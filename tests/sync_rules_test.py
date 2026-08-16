import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "sync_rules.py"
SPEC = importlib.util.spec_from_file_location("sync_rules", SCRIPT_PATH)
sync_rules = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = sync_rules
SPEC.loader.exec_module(sync_rules)


REPO_ROOT = Path(__file__).parent.parent


class SyncRulesTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)
        (self.root / "docs").mkdir()
        (self.root / ".claude" / "rules").mkdir(parents=True)
        self.addCleanup(self._temp.cleanup)

    def write_guide(self, body):
        (self.root / "docs" / "style-guide.md").write_text(body, encoding="utf-8")

    def rule_text(self, name):
        return (self.root / ".claude" / "rules" / f"{name}.md").read_text(
            encoding="utf-8"
        )

    def test_extracts_marked_section_with_paths(self):
        self.write_guide(
            '<!-- rule:cpp-style paths="**/*.cc,**/*.h" -->\n'
            "## C++\n\nUse absl::Status.\n"
            "<!-- /rule -->\n"
        )
        self.assertEqual(sync_rules.sync(self.root, check=False), [])

        text = self.rule_text("cpp-style")
        self.assertTrue(text.startswith('---\npaths:\n  - "**/*.cc"\n  - "**/*.h"\n---'))
        self.assertIn("Use absl::Status.", text)

    def test_promotes_headings_to_top_level(self):
        self.write_guide(
            "<!-- rule:testing -->\n"
            "## Testing\n\n### Determinism\n\nAssert it.\n"
            "<!-- /rule -->\n"
        )
        sync_rules.sync(self.root, check=False)

        text = self.rule_text("testing")
        self.assertIn("\n# Testing\n", text)
        self.assertIn("\n## Determinism\n", text)

    def test_rule_without_paths_has_no_frontmatter(self):
        self.write_guide("<!-- rule:always -->\n## Always\n\nRule.\n<!-- /rule -->\n")
        sync_rules.sync(self.root, check=False)

        self.assertFalse(self.rule_text("always").startswith("---"))

    def test_unmarked_content_is_not_extracted(self):
        self.write_guide(
            "# Guide\n\nRationale for humans.\n\n"
            "<!-- rule:cpp-style -->\n## C++\n\nRule.\n<!-- /rule -->\n\n"
            "More rationale.\n"
        )
        sync_rules.sync(self.root, check=False)

        text = self.rule_text("cpp-style")
        self.assertNotIn("Rationale for humans.", text)
        self.assertNotIn("More rationale.", text)

    def test_several_blocks_join_into_one_rule(self):
        self.write_guide(
            '<!-- rule:cpp-style paths="**/*.cc" -->\n## Naming\n\nFirst.\n<!-- /rule -->\n'
            "\nProse in between.\n\n"
            "<!-- rule:cpp-style -->\n## Errors\n\nSecond.\n<!-- /rule -->\n"
        )
        sync_rules.sync(self.root, check=False)

        text = self.rule_text("cpp-style")
        self.assertLess(text.index("First."), text.index("Second."))
        self.assertNotIn("Prose in between.", text)

    def test_markers_inside_a_code_fence_are_ignored(self):
        self.write_guide(
            "# Guide\n\n"
            "```\n<!-- rule:example paths=\"**/*.md\" -->\n```\n\n"
            "<!-- rule:real -->\n## Real\n\nRule.\n<!-- /rule -->\n"
        )
        sync_rules.sync(self.root, check=False)

        self.assertFalse((self.root / ".claude" / "rules" / "example.md").exists())
        self.assertTrue((self.root / ".claude" / "rules" / "real.md").exists())

    def test_check_reports_stale_file_without_writing(self):
        self.write_guide("<!-- rule:cpp-style -->\n## C++\n\nOriginal.\n<!-- /rule -->\n")
        sync_rules.sync(self.root, check=False)

        self.write_guide("<!-- rule:cpp-style -->\n## C++\n\nUpdated.\n<!-- /rule -->\n")
        stale = sync_rules.sync(self.root, check=True)

        self.assertEqual(len(stale), 1)
        self.assertIn("out of date", stale[0])
        self.assertIn("Original.", self.rule_text("cpp-style"))

    def test_check_reports_missing_file(self):
        self.write_guide("<!-- rule:cpp-style -->\n## C++\n\nRule.\n<!-- /rule -->\n")

        stale = sync_rules.sync(self.root, check=True)

        self.assertEqual(len(stale), 1)
        self.assertIn("missing", stale[0])

    def test_generated_file_is_deleted_when_its_section_leaves_the_guide(self):
        self.write_guide(
            "<!-- rule:gone -->\n## Gone\n\nRule.\n<!-- /rule -->\n"
            "<!-- rule:kept -->\n## Kept\n\nRule.\n<!-- /rule -->\n"
        )
        sync_rules.sync(self.root, check=False)

        self.write_guide("<!-- rule:kept -->\n## Kept\n\nRule.\n<!-- /rule -->\n")
        stale = sync_rules.sync(self.root, check=True)
        self.assertEqual(len(stale), 1)
        self.assertIn("no matching section", stale[0])

        sync_rules.sync(self.root, check=False)
        self.assertFalse((self.root / ".claude" / "rules" / "gone.md").exists())

    def test_hand_written_rule_file_is_left_alone(self):
        hand_written = self.root / ".claude" / "rules" / "hand-written.md"
        hand_written.write_text("# Not generated\n", encoding="utf-8")
        self.write_guide("<!-- rule:kept -->\n## Kept\n\nRule.\n<!-- /rule -->\n")

        sync_rules.sync(self.root, check=False)

        self.assertEqual(hand_written.read_text(encoding="utf-8"), "# Not generated\n")

    def test_unclosed_marker_is_an_error(self):
        self.write_guide("<!-- rule:cpp-style -->\n## C++\n\nRule.\n")

        with self.assertRaises(sync_rules.GuideError):
            sync_rules.sync(self.root, check=False)

    def test_nested_marker_is_an_error(self):
        self.write_guide(
            "<!-- rule:a -->\n## A\n\n<!-- rule:b -->\n## B\n<!-- /rule -->\n"
        )

        with self.assertRaises(sync_rules.GuideError):
            sync_rules.sync(self.root, check=False)

    def test_conflicting_paths_for_one_rule_is_an_error(self):
        self.write_guide(
            '<!-- rule:a paths="**/*.cc" -->\n## A\n\nRule.\n<!-- /rule -->\n'
            '<!-- rule:a paths="**/*.h" -->\n## A2\n\nRule.\n<!-- /rule -->\n'
        )

        with self.assertRaises(sync_rules.GuideError):
            sync_rules.sync(self.root, check=False)

    def test_guide_with_no_marked_sections_is_an_error(self):
        self.write_guide("# Guide\n\nNothing marked up.\n")

        with self.assertRaises(sync_rules.GuideError):
            sync_rules.sync(self.root, check=False)

    def test_repository_rules_are_in_sync_with_the_guide(self):
        # The same check build_and_test.sh runs. It fails when someone edits the
        # style guide and forgets to regenerate, or edits a rule file directly.
        self.assertEqual(sync_rules.sync(REPO_ROOT, check=True), [])


if __name__ == "__main__":
    unittest.main()
