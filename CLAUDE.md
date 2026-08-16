@AGENTS.md

# Claude Code

- CMake is the only supported build system; do not use Bazel.
- Rules under `.claude/rules/` are generated from `docs/style-guide.md`. Change
  the guide and run `scripts/sync_rules.py`; never edit a generated rule.
- `.claude/settings.json` formats C++ files after Claude uses Edit or Write.
