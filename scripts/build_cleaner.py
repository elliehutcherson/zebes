import argparse
import subprocess
import sys
import re
import os
import shutil
from pathlib import Path
import concurrent.futures

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
        # Check if the line is already commented out
        if re.match(r'^\s*(//|/\*)', content):
            log_info(f"Skipping line {line_num} (commented): {content.strip()}")
            continue
        candidates.append((line_num, idx, content))

    if not candidates:
        return 0

    if dry_run:
        for c in candidates:
            print(f"{YELLOW}[Dry Run] Would check removal of: {c[2].strip()} (Line {c[0]}){RESET}")
        return len(candidates)

    log_info(f"Verifying {len(candidates)} candidates for {file_path}...")
    
    # Attempt batch removal first
    if len(candidates) > 1:
        print(f"Attempting file-batch removal of {len(candidates)} includes... ", end='', flush=True)
        # Apply all removals
        for line_num, idx, content in candidates:
             lines[idx] = "// " + content
        
        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(lines)
            
        if run_build(build_dir):
            print(f"{GREEN}File-batch removal PASS.{RESET}")
            # All good, mark all for deletion
            for line_num, idx, content in candidates:
                lines[idx] = ""
            confirmed_removals = len(candidates)
            # Finish up
            final_output = [l for l in lines if l != ""]
            try:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.writelines(final_output)
            except Exception as e:
                log_error(f"Failed to write final changes to {file_path}: {e}")
            return confirmed_removals
        else:
            print(f"{RED}File-batch removal FAIL. Fallback to sequential.{RESET}")
            # Revert to clean state
            lines = list(original_lines)
            with open(file_path, 'w', encoding='utf-8') as f:
                f.writelines(lines)

    # We process one by one to correct build failures if batch failed or only 1 candidate.
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

def scan_file(file_path, clang_tidy_cmd, build_dir, extra_args, verbose):
    log_info(f"Scanning {file_path}...")
    output = run_clang_tidy_check(file_path, clang_tidy_cmd, build_dir, extra_args)
    if verbose:
        print(output)
    unused_lines = parse_unused_includes(output)
    return file_path, unused_lines

def apply_global_batch(all_candidates, build_dir):
    """
    Tries to apply all fixes at once.
    all_candidates: list of (file_path, unused_lines_list)
    """
    # Back up everything first? No, we rely on reversion.
    # We need to read all files, apply comments, save.
    
    modified_files = {} # path -> original_lines
    
    log_info(f"Attempting global batch removal of unused includes from {len(all_candidates)} files...")
    
    try:
        for file_path, unused_lines in all_candidates:
            with open(file_path, 'r', encoding='utf-8') as f:
                lines = f.readlines()
            
            modified_files[file_path] = list(lines) # Copy actual logic lines
            
            # Apply comments (reversed order usually safest but we have line numbers)
            # unused_lines is sorted descending.
            for line_num in unused_lines:
                if line_num < 1 or line_num > len(lines): continue
                idx = line_num - 1
                if "#include" in lines[idx] and not re.match(r'^\s*(//|/\*)', lines[idx]):
                    lines[idx] = "// " + lines[idx]
            
            with open(file_path, 'w', encoding='utf-8') as f:
                f.writelines(lines)
        
        # Build
        if run_build(build_dir):
            print(f"{GREEN}Global batch removal PASS.{RESET}")
            # Apply permanent deletions
            count = 0
            for file_path, unused_lines in all_candidates:
                 # Read current (commented)
                 with open(file_path, 'r', encoding='utf-8') as f:
                    lines = f.readlines()
                 
                 # Remove lines that were commented
                 # unused_lines is sorted desc
                 files_removed = 0
                 for line_num in unused_lines:
                     if line_num < 1 or line_num > len(lines): continue
                     idx = line_num - 1
                     if lines[idx].strip().startswith("// #include"):
                         lines[idx] = ""
                         files_removed += 1
                 
                 final_output = [l for l in lines if l != ""]
                 with open(file_path, 'w', encoding='utf-8') as f:
                     f.writelines(final_output)
                 count += files_removed
            return count
        else:
             print(f"{RED}Global batch removal FAIL.{RESET}")
             return -1
             
    except Exception as e:
        log_error(f"Global batch failed with exception: {e}")
        return -1
    finally:
        # If we failed (returned -1 or exception), we must revert ALL files to original state
        if 'count' not in locals(): # If we didn't succeed
            # Revert all files
            log_info("Reverting global batch changes...")
            for file_path, original_lines in modified_files.items():
                try:
                    with open(file_path, 'w', encoding='utf-8') as f:
                        f.writelines(original_lines)
                except Exception as e:
                    log_error(f"Failed to revert {file_path}: {e}")
            
    return -1

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

    # 1. Collect all files
    files_to_scan = []
    if target_path.is_file():
        files_to_scan.append(str(target_path))
    else:
        for root, dirs, files in os.walk(target_path):
            files.sort()
            for file in files:
                if file.endswith('.cc') or file.endswith('.cpp'):
                     if file.startswith('.'): continue
                     files_to_scan.append(os.path.join(root, file))

    log_info(f"Can parallel scan {len(files_to_scan)} files.")

    # 2. Parallel Scan
    results = [] # list of (file, unused_lines)
    # Using ProcessPoolExecutor for parallel processing
    with concurrent.futures.ProcessPoolExecutor() as executor:
        # Submit all tasks
        futures = {executor.submit(scan_file, f, clang_tidy_cmd, args.build_dir, extra_args, args.verbose): f for f in files_to_scan}
        
        for future in concurrent.futures.as_completed(futures):
            try:
                f_path, unused = future.result()
                if unused:
                     results.append((f_path, unused))
            except Exception as e:
                log_error(f"Scanning failed for {futures[future]}: {e}")

    total_candidates = sum(len(u) for f, u in results)
    if total_candidates == 0:
        print(f"\n{GREEN}No unused includes found.{RESET}")
        return

    if args.dry_run:
        print(f"\n{GREEN}Dry run complete. Found {total_candidates} potential removals across {len(results)} files.{RESET}")
        for f, unused in results:
             print(f"{f}: {len(unused)} unused")
        return

    # 3. Global Batch Attempt
    total_removed = 0
    
    # Only try global batch if we have multiple files or many candidates?
    # Actually always try it, it's one build.
    global_res = apply_global_batch(results, args.build_dir)
    
    if global_res >= 0:
        total_removed = global_res
    else:
        # 4. Fallback to sequential/per-file
        log_info("Falling back to per-file verification...")
        for f_path, unused in results:
            total_removed += verify_and_apply_fixes(f_path, unused, args.build_dir, args.dry_run)

    print(f"\n{GREEN}Cleanup complete. Removed {total_removed} includes.{RESET}")

if __name__ == "__main__":
    main()
