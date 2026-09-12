"""Verify the retained 3D-to-sprite artifact rather than inferred rig intent."""

import json
import hashlib
from pathlib import Path
import struct
import unittest

from PIL import Image

from scripts.review_mouse_3d import pack

ARTIFACT = Path(__file__).resolve().parents[1] / "experiments/mouse_3d/v1"


class Mouse3dAssetTest(unittest.TestCase):
    artifact = ARTIFACT

    def test_every_exported_frame_matches_sheet_and_apng(self):
        for size in (512, 128, 96):
            sheet = Image.open(self.artifact / f"sheet-{size}.png").convert("RGBA")
            animation = Image.open(self.artifact / f"run-{size}.png")
            self.addCleanup(animation.close)
            self.assertEqual(sheet.size, (size * 6, size * 4))
            self.assertEqual(animation.n_frames, 24)
            for index in range(24):
                x, y = index % 6 * size, index // 6 * size
                frame = sheet.crop((x, y, x + size, y + size))
                animation.seek(index)
                self.assertEqual(frame.tobytes(), animation.convert("RGBA").tobytes())
                bounds = frame.getchannel("A").point(lambda a: 255 if a >= 128 else 0).getbbox()
                self.assertIsNotNone(bounds)
                self.assertGreater(min(bounds[:2]), 0)
                self.assertLess(max(bounds[2:]), size)
                if size == 512:
                    raw = Image.open(self.artifact / "frames" / f"run-{index + 1:02}.png").convert("RGBA")
                    self.assertEqual(raw.tobytes(), frame.tobytes())

    def test_glb_has_real_geometry_skin_and_one_second_animation(self):
        data = (self.artifact / "mouse.glb").read_bytes()
        magic, version, total_length = struct.unpack_from("<4sII", data)
        self.assertEqual(magic, b"glTF")
        self.assertEqual(version, 2)
        self.assertEqual(total_length, len(data))
        json_length, chunk_type = struct.unpack_from("<I4s", data, 12)
        self.assertEqual(chunk_type, b"JSON")
        gltf = json.loads(data[20:20 + json_length])
        self.assertTrue(gltf["meshes"])
        self.assertTrue(gltf["skins"])
        self.assertTrue(gltf["animations"])
        for mesh in gltf["meshes"]:
            for primitive in mesh["primitives"]:
                self.assertIn("POSITION", primitive["attributes"])
                self.assertIn("JOINTS_0", primitive["attributes"])
                self.assertIn("WEIGHTS_0", primitive["attributes"])
        animation = gltf["animations"][0]
        minimum_time = min(gltf["accessors"][s["input"]]["min"][0] for s in animation["samplers"])
        maximum_time = max(gltf["accessors"][s["input"]]["max"][0] for s in animation["samplers"])
        self.assertEqual(minimum_time, 0)
        self.assertAlmostEqual(maximum_time, 1.0)

    def test_blender_checks_cover_all_frames_and_soles(self):
        manifest = json.loads((self.artifact / "manifest.json").read_text())
        self.assertEqual(len(manifest["geometry_checks"]), 24)
        self.assertEqual(len(manifest["poses"]), 25)
        for row in manifest["geometry_checks"]:
            self.assertLess(row["maximum_joint_error"], 1e-4)
            for side in ("near", "far"):
                self.assertGreaterEqual(row["sole_minimum_z"][side], -1e-5)
        self.assertEqual(manifest["poses"][0]["bones"], manifest["poses"][-1]["bones"])

    def test_sheet_packing_rejects_partial_rows_and_size_changes(self):
        with self.assertRaises(ValueError):
            pack([Image.new("RGBA", (32, 32))] * 5)
        with self.assertRaises(ValueError):
            pack([Image.new("RGBA", (32, 32))] * 5 + [Image.new("RGBA", (16, 16))])


class Mouse3dHoodDownAssetTest(Mouse3dAssetTest):
    artifact = ARTIFACT.parent / "v2"

    def test_head_is_one_manifold_skin_and_glb_retains_its_color(self):
        manifest = json.loads((self.artifact / "manifest.json").read_text())
        self.assertTrue(manifest["head_surface_check"]["closed_manifold"])
        self.assertEqual(manifest["head_surface_check"]["connected_components"], 1)
        data = (self.artifact / "mouse.glb").read_bytes()
        length = struct.unpack_from("<I", data, 12)[0]
        gltf = json.loads(data[20:20+length])
        head = next(mesh for mesh in gltf["meshes"] if mesh["name"] == "Head_continuous")
        for primitive in head["primitives"]:
            self.assertIn("COLOR_0", primitive["attributes"])

    def test_retained_sources_match_render_manifest(self):
        manifest = json.loads((self.artifact / "manifest.json").read_text())
        for name, digest in manifest["script_sha256"].items():
            self.assertEqual(hashlib.sha256((self.artifact / "source" / name).read_bytes()).hexdigest(), digest)


class Mouse3dRefinedAssetTest(Mouse3dHoodDownAssetTest):
    artifact = ARTIFACT.parent / "v3"

    def test_folded_cloth_remains_closed_and_portable_details_retain_colors(self):
        manifest = json.loads((self.artifact / "manifest.json").read_text())
        for side in ("near", "far"):
            check = manifest["cloth_surface_checks"][side]
            self.assertTrue(check["closed_manifold"])
            self.assertEqual(check["connected_components"], 1)
        data = (self.artifact / "mouse.glb").read_bytes()
        length = struct.unpack_from("<I", data, 12)[0]
        gltf = json.loads(data[20:20+length])
        for name in ("Trousers_near", "Trousers_far", "Eye_lens_near", "Eye_lens_far"):
            mesh = next(mesh for mesh in gltf["meshes"] if mesh["name"] == name)
            for primitive in mesh["primitives"]:
                self.assertIn("COLOR_0", primitive["attributes"])


if __name__ == "__main__":
    unittest.main()
