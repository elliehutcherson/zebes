import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

class BuildCleanerTest(unittest.TestCase):
    def setUp(self):
        # Create a temporary directory for the workspace
        self.test_dir = tempfile.mkdtemp()
        self.build_dir = os.path.join(self.test_dir, 'build')
        self.src_dir = os.path.join(self.test_dir, 'src')
        os.makedirs(self.src_dir)
        
        # Define paths
        self.script_path = os.path.abspath(os.path.join(os.path.dirname(__file__), '../scripts/build_cleaner.py'))
        self.cpp_file = os.path.join(self.src_dir, 'main.cc')
        self.header_file = os.path.join(self.src_dir, 'unused.h')
        self.cmakelists = os.path.join(self.test_dir, 'CMakeLists.txt')

    def tearDown(self):
        shutil.rmtree(self.test_dir)

    def write_file(self, path, content):
        with open(path, 'w') as f:
            f.write(content)

    def test_remove_unused_include(self):
        # 1. Setup project
        self.write_file(self.header_file, "#pragma once\nvoid foo() {}")
        
        # main.cc with unused include
        main_content = """#include <iostream>
#include "unused.h" // Unused

int main() {
    std::cout << "Hello" << std::endl;
    return 0;
}
"""
        self.write_file(self.cpp_file, main_content)

        # CMakeLists.txt
        cmake_content = """cmake_minimum_required(VERSION 3.10)
project(TestProject)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include_directories(src)
add_executable(main src/main.cc)
"""
        self.write_file(self.cmakelists, cmake_content)

        # 2. Configure cmake to generate compile_commands.json
        subprocess.check_call(
            ['cmake', '-S', self.test_dir, '-B', self.build_dir, '-DCMAKE_BUILD_TYPE=Debug'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        
        # 3. Build initial (should pass)
        subprocess.check_call(
            ['cmake', '--build', self.build_dir],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )

        # 4. Run build_cleaner.py
        # We assume the script is executable or verify if we need to call with python3
        cmd = [sys.executable, self.script_path, '--build-dir', self.build_dir, self.cpp_file]
        
        # Run process
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        # Check output for success
        self.assertEqual(result.returncode, 0, f"Script failed with output:\n{result.stdout}\n{result.stderr}")
        self.assertIn("Cleanup complete", result.stdout)

        # 5. Verify file content
        with open(self.cpp_file, 'r') as f:
            content = f.read()
        
        # The line '#include "unused.h"' should be gone or empty
        self.assertNotIn('#include "unused.h"', content)
        self.assertIn('#include <iostream>', content)

if __name__ == '__main__':
    unittest.main()
