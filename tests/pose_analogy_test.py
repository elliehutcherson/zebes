import json
import math
import unittest
from pathlib import Path

from scripts.prepare_pose_analogy import (BODY_CELLS, CELL, CELL_PIXELS, COLUMNS, FAR_BONES,
                                          MOUSE_GROUND_Y, NEAR_BONES, ROWS_UP, confidence_of,
                                          cycle_anchor, derive_arms, fit_leg_scale, foot_contact,
                                          grid_frame, grounded, hip_bob, nearest_to_rest,
                                          total_leg_length, viewport)
from scripts.retarget_run_reference import transfer

ROOT = Path(__file__).resolve().parent.parent
TRACE = ROOT / "experiments/pose_analogy/inputs/reference-run-trace-v1.json"
STRIP = ROOT / "experiments/pose_analogy/inputs/reference-run-10.png"
DOCUMENT = ROOT / "experiments/character_binding/puppet_documents/mouse_run_reference_v2.json"

JOINTS = ["hip_c", "neck", "head_top",
          "shoulder_l", "elbow_l", "wrist_l", "paw_l",
          "shoulder_r", "elbow_r", "wrist_r", "paw_r",
          "knee_l", "ankle_l", "toe_l", "knee_r", "ankle_r", "toe_r"]


def load(path):
    return json.loads(path.read_text())


class ReferenceTraceTest(unittest.TestCase):
    def setUp(self):
        self.trace = load(TRACE)

    def test_every_frame_traces_every_joint(self):
        self.assertEqual(len(self.trace["frames"]), 10)
        for frame in self.trace["frames"] + [self.trace["neutral"]]:
            self.assertEqual(sorted(frame["pose"]), sorted(JOINTS), frame["name"])

    def test_every_cell_lies_inside_the_retained_strip(self):
        from PIL import Image

        with Image.open(STRIP) as strip:
            width, height = strip.size
        self.assertEqual((width, height), (CELL * 10, CELL))
        for index, frame in enumerate(self.trace["frames"]):
            self.assertEqual(frame["cell"], [CELL * index, 0, CELL, CELL], frame["name"])
            for name, (x, y) in frame["pose"].items():
                self.assertTrue(0 <= x < CELL and 0 <= y < CELL, f"{frame['name']} {name}")

    def test_the_cycle_has_two_flight_frames_in_opposite_halves(self):
        supports = [frame["support"] for frame in self.trace["frames"]]
        flight = [index for index, support in enumerate(supports) if support == "flight"]
        self.assertEqual(len(flight), 2, supports)
        self.assertEqual(abs(flight[0] - flight[1]), 5, flight)
        for index in flight:
            self.assertEqual(supports[(index - 1) % 10], "near" if index == 3 else "far")

    def test_a_flight_frame_never_reaches_as_low_as_a_supported_frame(self):
        rows = {frame["support"]: [] for frame in self.trace["frames"]}
        for frame in self.trace["frames"]:
            rows[frame["support"]].append(frame["lowest_row"])
        self.assertLess(max(rows["flight"]), min(rows["near"] + rows["far"]))

    def test_support_alternates_in_runs_not_frame_by_frame(self):
        supports = [frame["support"] for frame in self.trace["frames"]]
        planted = [support for support in supports if support != "flight"]
        self.assertEqual(planted.count("near"), 4)
        self.assertEqual(planted.count("far"), 4)


class DerivedArmsTest(unittest.TestCase):
    def test_elbow_and_wrist_land_between_the_shoulder_and_the_paw(self):
        pose = {"shoulder_l": [0, 0], "paw_l": [10, 0], "elbow_l": [5, 5], "wrist_l": [9, 9],
                "shoulder_r": [0, 0], "paw_r": [0, 10], "elbow_r": [1, 1], "wrist_r": [2, 2]}
        resolved = derive_arms(pose)
        self.assertAlmostEqual(resolved["elbow_l"][0], 4.5)
        self.assertAlmostEqual(resolved["wrist_l"][0], 7.8)
        for side in ["l", "r"]:
            shoulder, paw = pose["shoulder_" + side], pose["paw_" + side]
            span = math.dist(shoulder, paw)
            for name in ["elbow_", "wrist_"]:
                self.assertLess(math.dist(shoulder, resolved[name + side]), span)

    def test_the_bow_always_falls_on_the_lower_side_of_the_arm(self):
        for paw in [[10, 0], [-10, 0], [0, 10], [7, -7]]:
            resolved = derive_arms({"shoulder_l": [0, 0], "paw_l": paw,
                                    "shoulder_r": [0, 0], "paw_r": [1, 1]})
            midpoint = [(paw[0]) * 0.45, (paw[1]) * 0.45]
            self.assertGreater(resolved["elbow_l"][1], midpoint[1] - 1e-9, paw)

    def test_a_collapsed_arm_fails_rather_than_guessing_a_direction(self):
        with self.assertRaises(ValueError):
            derive_arms({"shoulder_l": [4, 4], "paw_l": [4, 4],
                         "shoulder_r": [0, 0], "paw_r": [1, 1]})


class GridTest(unittest.TestCase):
    def test_the_cell_is_a_twelfth_of_the_character_s_own_height(self):
        frame = grid_frame({"hip_c": [10, 40], "neck": [10, 30], "head_top": [10, 16]}, 64)
        self.assertAlmostEqual(frame["cell"], 48 / BODY_CELLS)
        self.assertEqual(frame["origin"], [10.0, 64.0])

    def test_ears_above_the_head_top_raise_the_measured_height(self):
        pose = {"hip_c": [10, 40], "neck": [10, 30], "head_top": [10, 16], "ear_l": [6, 8],
                "ear_r": [14, 12]}
        self.assertAlmostEqual(grid_frame(pose, 64)["cell"], 56 / BODY_CELLS)

    def test_a_hip_at_or_below_the_ground_row_fails(self):
        for ground in [40, 30]:
            with self.assertRaises(ValueError):
                grid_frame({"hip_c": [10, 40], "neck": [10, 30], "head_top": [10, 16]}, ground)

    def test_two_characters_of_different_scale_share_one_panel_mapping(self):
        small = grid_frame({"hip_c": [38, 48], "neck": [38, 37], "head_top": [38, 25]}, 67)
        large = grid_frame({"hip_c": [141, 153], "neck": [141, 105], "head_top": [141, 48]}, 189)
        for frame in [small, large]:
            source_x, source_y, scale = viewport(frame)
            origin = frame["origin"]
            self.assertAlmostEqual((origin[0] - source_x) * scale, COLUMNS * CELL_PIXELS)
            self.assertAlmostEqual((origin[1] - source_y) * scale, ROWS_UP * CELL_PIXELS)
            top = frame["rows"]["head_top"]
            self.assertAlmostEqual((top - source_y) * scale,
                                   (ROWS_UP - BODY_CELLS) * CELL_PIXELS)


class CycleAnchorTest(unittest.TestCase):
    def test_the_anchor_takes_median_rows_from_the_cycle(self):
        poses = [{"hip_c": [10, 40], "neck": [10, 30], "head_top": [10, 20]},
                 {"hip_c": [12, 44], "neck": [12, 34], "head_top": [12, 24]},
                 {"hip_c": [14, 48], "neck": [14, 38], "head_top": [14, 28]}]
        anchor = cycle_anchor(poses)
        self.assertEqual(anchor["hip_c"], [12.0, 44.0])
        self.assertEqual(anchor["neck"], [12.0, 34.0])

    def test_the_body_top_is_the_highest_point_any_frame_reaches(self):
        poses = [{"hip_c": [10, 40], "neck": [10, 30], "head_top": [10, 20]},
                 {"hip_c": [10, 40], "neck": [10, 30], "head_top": [10, 14]}]
        self.assertEqual(cycle_anchor(poses)["head_top"][1], 14)

    def test_ears_reaching_above_the_head_top_set_the_body_top(self):
        poses = [{"hip_c": [10, 40], "neck": [10, 30], "head_top": [10, 20], "ear_l": [8, 11]}]
        self.assertEqual(cycle_anchor(poses)["head_top"][1], 11)

    def test_an_empty_cycle_fails_rather_than_inventing_an_anchor(self):
        with self.assertRaises(ValueError):
            cycle_anchor([])

    def test_the_real_cycle_anchor_sits_between_the_frames_it_summarises(self):
        trace = load(TRACE)
        poses = [derive_arms(frame["pose"], confidence_of(frame)) for frame in trace["frames"]]
        anchor = cycle_anchor(poses)
        hips = [pose["hip_c"][1] for pose in poses]
        self.assertLessEqual(min(hips), anchor["hip_c"][1])
        self.assertLessEqual(anchor["hip_c"][1], max(hips))
        self.assertLess(anchor["head_top"][1], anchor["neck"][1])
        self.assertLess(anchor["neck"][1], anchor["hip_c"][1])
        self.assertLess(anchor["hip_c"][1], trace["ground_y"])


class RequestSetTest(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads(
            (ROOT / "experiments/pose_analogy/evidence/analogy-inputs-v1/manifest.json").read_text())
        self.request = ROOT / "experiments/pose_analogy/evidence/analogy-inputs-v1/request"

    def test_five_poses_are_requested(self):
        self.assertEqual(len(self.manifest["request_frames"]), 5)

    def test_the_request_covers_both_support_feet_and_a_flight_phase(self):
        supports = set(self.manifest["request_supports"])
        self.assertEqual(supports, {"near", "far", "flight"})

    def test_each_requested_pose_has_one_reference_and_one_mouse_image(self):
        for name in self.manifest["request_frames"]:
            self.assertTrue((self.request / f"reference-{name}.png").is_file(), name)
            self.assertTrue((self.request / f"mouse-{name}.png").is_file(), name)
        self.assertEqual(len(list(self.request.glob("*.png"))), 11)
        self.assertTrue((self.request / "character-reference.png").is_file())

    def test_exactly_one_mouse_card_carries_the_character_artwork(self):
        from PIL import Image

        coloured = []
        for name in self.manifest["request_frames"]:
            with Image.open(self.request / f"mouse-{name}.png") as image:
                pixels = image.convert("RGB").getcolors(maxcolors=1 << 20)
            coloured.append((name, len(pixels)))
        richest = max(coloured, key=lambda entry: entry[1])
        self.assertEqual(richest[0], self.manifest["artwork_card"])
        others = [count for name, count in coloured if name != self.manifest["artwork_card"]]
        self.assertGreater(richest[1], max(others) * 2)

    def test_nothing_in_the_package_claims_to_have_been_submitted(self):
        self.assertFalse(self.manifest["submitted"])


class TransferTest(unittest.TestCase):
    def setUp(self):
        self.trace = load(TRACE)
        self.rest = load(DOCUMENT)["rest_pose"]
        self.poses = [derive_arms(frame["pose"], confidence_of(frame))
                      for frame in self.trace["frames"]]
        self.supports = [frame["support"] for frame in self.trace["frames"]]

    def normalised(self):
        index = max((index for index, support in enumerate(self.supports) if support != "flight"),
                    key=lambda index: total_leg_length(self.poses[index]))
        return {"name": self.trace["frames"][index]["name"], "support": self.supports[index],
                "pose": self.poses[index]}

    def test_the_normaliser_is_the_most_extended_supported_frame(self):
        normaliser = self.normalised()
        self.assertNotEqual(normaliser["support"], "flight")
        for pose, support in zip(self.poses, self.supports, strict=True):
            if support != "flight":
                self.assertLessEqual(total_leg_length(pose),
                                     total_leg_length(normaliser["pose"]) + 1e-9)

    def test_the_fitted_scale_stands_the_extended_pose_on_its_own_hip_row(self):
        normaliser = self.normalised()
        factor = fit_leg_scale(self.rest, normaliser, MOUSE_GROUND_Y)
        scaled = {n: [x * factor, y * factor] for n, (x, y) in normaliser["pose"].items()}
        posed = transfer(self.rest, normaliser["pose"], scaled)
        side = "l" if normaliser["support"] == "near" else "r"
        hip = posed["hip_c"][1] + (MOUSE_GROUND_Y - posed["toe_" + side][1])
        self.assertAlmostEqual(hip, self.rest["hip_c"][1], places=3)

    def test_a_flight_normaliser_fails_rather_than_grounding_a_lifted_foot(self):
        with self.assertRaises(ValueError):
            fit_leg_scale(self.rest, {"name": "x", "support": "flight", "pose": self.poses[3]},
                          MOUSE_GROUND_Y)

    def test_no_frame_stretches_the_legs_past_the_mouse_s_own_reach(self):
        normaliser = self.normalised()
        factor = fit_leg_scale(self.rest, normaliser, MOUSE_GROUND_Y)
        scaled = {n: [x * factor, y * factor] for n, (x, y) in normaliser["pose"].items()}
        reach = total_leg_length(self.rest)
        for pose in self.poses:
            self.assertLessEqual(total_leg_length(transfer(self.rest, pose, scaled)), reach + 1e-6)

    def test_grounding_seats_supports_exactly_and_lifts_flight_frames_clear(self):
        normaliser = self.normalised()
        factor = fit_leg_scale(self.rest, normaliser, MOUSE_GROUND_Y)
        scaled = {n: [x * factor, y * factor] for n, (x, y) in normaliser["pose"].items()}
        posed = grounded([transfer(self.rest, pose, scaled) for pose in self.poses],
                         self.supports, MOUSE_GROUND_Y)
        for pose, support in zip(posed, self.supports, strict=True):
            if support == "flight":
                self.assertLess(max(foot_contact(pose, "l"), foot_contact(pose, "r")),
                                MOUSE_GROUND_Y)
                continue
            side = "l" if support == "near" else "r"
            self.assertAlmostEqual(foot_contact(pose, side), MOUSE_GROUND_Y)

    def grounded_cycle(self):
        normaliser = self.normalised()
        factor = fit_leg_scale(self.rest, normaliser, MOUSE_GROUND_Y)
        scaled = {n: [x * factor, y * factor] for n, (x, y) in normaliser["pose"].items()}
        return grounded([transfer(self.rest, pose, scaled) for pose in self.poses],
                        self.supports, MOUSE_GROUND_Y)

    def test_the_hip_stays_near_its_own_standing_row(self):
        standing = MOUSE_GROUND_Y - self.rest["hip_c"][1]
        for pose, support in zip(self.grounded_cycle(), self.supports, strict=True):
            height = MOUSE_GROUND_Y - pose["hip_c"][1]
            self.assertLess(abs(height - standing), standing * 0.4, support)

    def test_no_frame_drives_a_foot_through_the_ground(self):
        for pose, support in zip(self.grounded_cycle(), self.supports, strict=True):
            for side in ["l", "r"]:
                self.assertLessEqual(pose["toe_" + side][1], MOUSE_GROUND_Y + 1e-6, support)
                self.assertLessEqual(pose["ankle_" + side][1], MOUSE_GROUND_Y + 1e-6, support)

    def test_the_hip_bob_gap_against_the_reference_is_recorded(self):
        measured = hip_bob(self.grounded_cycle(), self.rest, MOUSE_GROUND_Y, self.trace)
        self.assertGreater(measured["ratio"], 0)
        self.assertGreater(measured["reference_ratio"], 0)
        # The mouse bobs more than the reference because its bind legs start far
        # more bent. Asserted as a known, bounded gap so a later leg-IK pass has a
        # number to beat and a silent regression past it still fails.
        self.assertLess(measured["ratio"], measured["reference_ratio"] * 2.0)

    def test_adjacent_flight_frames_fail_rather_than_inventing_a_ground(self):
        poses = [{"toe_l": [0, 0], "toe_r": [0, 0], "ankle_l": [0, 0], "ankle_r": [0, 0],
                  "hip_c": [0, 0]} for _ in range(4)]
        with self.assertRaises(ValueError):
            grounded(poses, ["near", "flight", "flight", "far"], MOUSE_GROUND_Y)

    def test_near_and_far_bones_name_disjoint_limbs(self):
        near = {joint for bone in NEAR_BONES for joint in bone} - {"hip_c"}
        far = {joint for bone in FAR_BONES for joint in bone} - {"hip_c"}
        self.assertEqual(near & far, set())


if __name__ == "__main__":
    unittest.main()
