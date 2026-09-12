"""Offline boundaries for the bounded image-to-3D trial runner."""

import json
from pathlib import Path
import tempfile
import unittest

from scripts.run_storybook_shape import digest, new_output, validate_prepared


class StorybookShapeTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name)
        self.manifest = {}
        for name, key in (("source.png", "source_sha256"), ("masked-input.png", "masked_sha256"),
                          ("conditioning-image.png", "conditioning_sha256"), ("conditioning-mask.png", "mask_sha256")):
            (self.path / name).write_bytes(name.encode())
            self.manifest[key] = digest(self.path / name)
        (self.path / "inputs.json").write_text(json.dumps(self.manifest))

    def test_inspected_inputs_are_accepted_without_modification(self):
        self.assertEqual(validate_prepared(self.path), self.manifest)

    def test_changed_mask_or_source_is_rejected_before_provider_call(self):
        for name in ("source.png", "masked-input.png", "conditioning-image.png", "conditioning-mask.png"):
            with self.subTest(name=name):
                original = (self.path / name).read_bytes()
                (self.path / name).write_bytes(b"different")
                with self.assertRaisesRegex(ValueError, "Prepared input changed"):
                    validate_prepared(self.path)
                (self.path / name).write_bytes(original)

    def test_missing_input_fails_instead_of_substituting_another_image(self):
        (self.path / "conditioning-mask.png").unlink()
        with self.assertRaises(FileNotFoundError):
            validate_prepared(self.path)

    def test_existing_output_is_preserved(self):
        with self.assertRaisesRegex(ValueError, "Output already exists"):
            new_output(self.path)
        self.assertTrue((self.path / "source.png").is_file())
        new_output(self.path / "new")
        self.assertTrue((self.path / "new").is_dir())


if __name__ == "__main__":
    unittest.main()
