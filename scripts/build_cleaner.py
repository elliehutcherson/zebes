
import argparse
import subprocess
import sys
import re
import os
import shutil
from pathlib import Path

# ANSI color codes
GREEN = '\033[92m'
RED = '\033[91m'
YELLOW = '\033[93m'
RESET = '\033[0m'

def log_info(msg):
    print(f"{GREEN}[INFO]{RESET} {msg}")

def log_warn(msg):
    print(f"{YELLOW}[WARN]{RESET} {msg}")

def log_error(msg):
    print(f"{RED}[ERROR]{RESET} {msg}")

def get_clang_tidy_path():
    common_paths = [
        '/usr/local/opt/llvm/bin/clang-tidy',
        '/opt/homebrew/opt/llvm/bin/clang-tidy',
        '/usr/bin/clang-tidy',
        '/usr/local/bin/clang-tidy',
    ]
    try:
        subprocess.run(['clang-tidy', '--version'], capture_output=True, check=True)
        return 'clang-tidy'
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass
    for p in common_paths:
        if os.path.exists(p):
            return p
    return None

def get_sysroot_flag():
    if sys.platform != 'darwin':
        return []
    try:
        result = subprocess.run(['xcrun', '--show-sdk-path'], capture_output=True, text=True, check=True)
        sdk_path = result.stdout.strip()
        if sdk_path and os.path.exists(sdk_path):
            log_info(f"Detected macOS SDK at {sdk_path}")
            return ['-isysroot', sdk_path]
    except Exception as e:
        log_warn(f"Failed to detect macOS SDK: {e}")
    return []

def run_clang_tidy_check(file_path, clang_tidy_cmd, build_dir, extra_args):
    """
    Runs clang-tidy on the file and returns the stdout output.
    """
    cmd = [
        clang_tidy_cmd,
        '-p', build_dir,
        '-checks=-*,misc-include-cleaner,readability-duplicate-include',
        '--quiet',
        file_path
    ]
    if extra_args:
        for arg in extra_args:
            cmd.append(f'-extra-arg={arg}')
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        return result.stdout
    except Exception as e:
        log_error(f"Failed to run clang-tidy on {file_path}: {e}")
        return ""

def parse_unused_includes(clang_tidy_stdout):
    unused_includes = []
    regex = re.compile(r':(\d+):\d+: warning: included header .* is not used directly \[misc-include-cleaner\]')
    regex_dup = re.compile(r':(\d+):\d+: warning: duplicate include \[readability-duplicate-include\]')

    for line in clang_tidy_stdout.splitlines():
        match = regex.search(line)
        if match:
            unused_includes.append(int(match.group(1)))
            continue
        match_dup = regex_dup.search(line)
        if match_dup:
            unused_includes.append(int(match_dup.group(1)))
    
    # Return sorted descending to keep indices valid if we were removing (though we use line numbers)
    return sorted(list(set(unused_includes)), reverse=True)

def run_build(build_dir):
    """
    Runs cmake --build. Returns True if success, False otherwise.
    """
    try:
        # We use -j to build fast. 
        # Using subprocess.run to check return code.
        # We capture output to avoid spamming the console, unless verbose?
        # For now, suppress output unless failure (could be huge).
        
        result = subprocess.run(
            ['cmake', '--build', build_dir, '-j', str(os.cpu_count() or 4)],
            capture_output=True,
            text=True
        )
        return result.returncode == 0
    except Exception as e:
        log_error(f"Build execution failed: {e}")
        return False

def verify_and_apply_fixes(file_path, unused_lines, build_dir, dry_run=False):
    if not unused_lines:
        return 0

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            original_lines = f.readlines()
    except Exception as e:
        log_error(f"Could not read {file_path}: {e}")
        return 0

    lines = list(original_lines) # Working copy
    confirmed_removals = 0

    # Filter out commented lines first
    candidates = []
    for line_num in unused_lines:
        if line_num < 1 or line_num > len(lines):
            continue
        idx = line_num - 1
        content = lines[idx]
        if "#include" not in content:
            continue
        if re.search(r'//|/\*', content):
            log_info(f"Skipping line {line_num} (commented): {content.strip()}")
            continue
        candidates.append((line_num, idx, content))

    if not candidates:
        return 0

    if dry_run:
        for c in candidates:
            print(f"{YELLOW}[Dry Run] Would check removal of: {c[2].strip()} (Line {c[0]}){RESET}")
        return len(candidates)

    log_info(f"Verifying {len(candidates)} candidates only for {file_path}...")

    # We process one by one to correct build failures.
    # Optimization: One by one is safe.
    
    for line_num, idx, content in candidates:
        print(f"Checking candidate: {content.strip()} (Line {line_num})... ", end='', flush=True)
        
        # 1. Modify: Comment out
        lines[idx] = "// " + content
        
        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(lines)
            
        # 2. Build
        if run_build(build_dir):
            print(f"{GREEN}Build PASS. Removing.{RESET}")
            # Build passed, we can remove this line permanently (or keep it commented? User said remove)
            # We will mark it for final writing.
            # Currently it is commented out in 'lines'.
            # If we want to delete it completely:
            # We must be careful about indices if we delete now.
            # Easiest: keep it commented in 'lines' for now, or replace with empty string?
            # Replace with empty string or specific marker to filter later.
            lines[idx] = "" # Mark for deletion
            confirmed_removals += 1
        else:
            print(f"{RED}Build FAIL. Keeping.{RESET}")
            # Build failed, revert this change
            lines[idx] = content
            # Write back immediately to restore clean state for next check
            with open(file_path, 'w', encoding='utf-8') as f:
                f.writelines(lines)

    # Final pass: Write file skipping empty lines that we removed
    final_output = [l for l in lines if l != ""]
    
    # Only write if we changed something (candidates exist and at least one removed, 
    # OR we reverted everything but we want to ensure file is identical to 'lines' state)
    # Actually 'lines' holds the current valid state (reverted or removed).
    try:
        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(final_output)
    except Exception as e:
        log_error(f"Failed to write final changes to {file_path}: {e}")

    return confirmed_removals

def process_file(file_path, clang_tidy_cmd, build_dir, extra_args, dry_run=False, verbose=False):
    log_info(f"Scanning {file_path}...")
    output = run_clang_tidy_check(file_path, clang_tidy_cmd, build_dir, extra_args)
    if verbose:
        print(output)
        
    unused_lines = parse_unused_includes(output)
    return verify_and_apply_fixes(file_path, unused_lines, build_dir, dry_run)

def main():
    parser = argparse.ArgumentParser(description='Remove unused includes using clang-tidy and build verification.')
    parser.add_argument('path', help='File or directory to scan')
    parser.add_argument('--build-dir', default='build', help='Path to build directory (default: build)')
    parser.add_argument('--dry-run', action='store_true', help='Do not modify files, just list candidates')
    parser.add_argument('--verbose', action='store_true', help='Print verbose output')
    args = parser.parse_args()

    clang_tidy_cmd = get_clang_tidy_path()
    if not clang_tidy_cmd:
        log_error("Could not find clang-tidy.")
        sys.exit(1)
        
    log_info(f"Using clang-tidy: {clang_tidy_cmd}")
    
    # Ensure build dir exists and valid
    if not os.path.exists(os.path.join(args.build_dir, 'CMakeCache.txt')):
         log_error(f"{args.build_dir} does not appear to be a valid CMake build directory.")
         sys.exit(1)

    extra_args = get_sysroot_flag()
    target_path = Path(args.path).resolve()
    
    if not target_path.exists():
        log_error(f"Path does not exist: {target_path}")
        sys.exit(1)
        
    # Check if we can build first?
    if not args.dry_run:
        log_info("Verifying initial build state...")
        if not run_build(args.build_dir):
            log_error("Initial build failed! Please fix build errors before running cleaner.")
            sys.exit(1)
        log_info("Initial build passed.")

    total_removed = 0
    if target_path.is_file():
        total_removed += process_file(str(target_path), clang_tidy_cmd, args.build_dir, extra_args, args.dry_run, args.verbose)
    else:
        for root, dirs, files in os.walk(target_path):
            for file in files:
                if file.endswith('.cc') or file.endswith('.cpp'):
                     if file.startswith('.'): continue
                     file_path = os.path.join(root, file)
                     total_removed += process_file(file_path, clang_tidy_cmd, args.build_dir, extra_args, args.dry_run, args.verbose)

    if args.dry_run:
        print(f"\n{GREEN}Dry run complete. Found potential removals.{RESET}")
    else:
        print(f"\n{GREEN}Cleanup complete. Removed {total_removed} includes.{RESET}")

if __name__ == "__main__":
    main()
