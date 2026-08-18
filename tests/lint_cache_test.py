import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "lint_cache.py"


class LintCacheTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name).resolve()
        self.source_dir = self.root / "src"
        self.build_dir = self.root / "build" / "dev"
        self.source_dir.mkdir()
        self.build_dir.mkdir(parents=True)
        (self.root / ".clang-tidy").write_text("Checks: '-*'\n", encoding="utf-8")
        self.header = self.source_dir / "value.h"
        self.header.write_text("constexpr int kValue = 1;\n", encoding="utf-8")
        self.unrelated_header = self.source_dir / "unrelated.h"
        self.unrelated_header.write_text("constexpr int kOther = 2;\n", encoding="utf-8")
        self.source = self.source_dir / "value.cc"
        self.source.write_text('#include "value.h"\nint Value() { return kValue; }\n', encoding="utf-8")
        self.fake_tidy = self.root / "clang-tidy"
        self.fake_tidy.write_text("fake clang-tidy binary\n", encoding="utf-8")
        commands = [
            {
                "directory": str(self.build_dir),
                "arguments": [
                    "c++",
                    f"-I{self.source_dir}",
                    "-c",
                    str(self.source),
                    "-o",
                    "value.cc.o",
                ],
                "file": str(self.source),
                "output": str(self.build_dir / "value.cc.o"),
            }
        ]
        (self.build_dir / "compile_commands.json").write_text(
            json.dumps(commands), encoding="utf-8"
        )

    def cache_key(self, strict=False):
        strict_args = ["--strict"] if strict else []
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--project-root",
                str(self.root),
                "--build-dir",
                str(self.build_dir),
                "--clang-tidy",
                str(self.fake_tidy),
                "--state-dir",
                str(self.build_dir / ".lint-cache"),
                *strict_args,
                str(self.source),
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()

    def test_key_tracks_transitive_inputs_but_not_unrelated_headers(self):
        initial = self.cache_key()
        self.assertEqual(len(initial), 64)
        self.assertEqual(self.cache_key(), initial)
        self.assertNotEqual(self.cache_key(strict=True), initial)

        self.unrelated_header.write_text("constexpr int kOther = 3;\n", encoding="utf-8")
        self.assertEqual(self.cache_key(), initial)

        self.header.write_text("constexpr int kValue = 4;\n", encoding="utf-8")
        after_header_change = self.cache_key()
        self.assertNotEqual(after_header_change, initial)

        (self.root / ".clang-tidy").write_text(
            "Checks: 'google-*'\n", encoding="utf-8"
        )
        self.assertNotEqual(self.cache_key(), after_header_change)


if __name__ == "__main__":
    unittest.main()
