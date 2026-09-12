import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.trace_sprite_sheet import (JOINTS, png_size, support_conflict,
                                        validate)

ROOT = Path(__file__).resolve().parent.parent
TRACE = ROOT / "experiments/pose_analogy/inputs/reference-run-trace-v1.json"
STRIP = ROOT / "experiments/pose_analogy/inputs/reference-run-10.png"
SHEET = (800, 80)


def complete_frame(name="reference_01", cell=(0, 0, 80, 80), support="near"):
    """A frame that is legal on every rule, so a test breaks only what it changes.

    The legs are laid out with the near foot planted lower than the far one, which
    is what support="near" claims, so the support check passes until a test moves
    them on purpose.
    """
    pose = {joint: [10 + index, 20 + index] for index, joint in enumerate(JOINTS)}
    pose["ankle_l"], pose["toe_l"] = [30, 60], [34, 65]
    pose["ankle_r"], pose["toe_r"] = [44, 48], [48, 53]
    if support == "far":
        pose["ankle_l"], pose["ankle_r"] = pose["ankle_r"], pose["ankle_l"]
        pose["toe_l"], pose["toe_r"] = pose["toe_r"], pose["toe_l"]
    return {"name": name, "cell": list(cell), "support": support, "pose": pose,
            "confidence": {joint: "observed" for joint in JOINTS}}


def complete_trace(frames=None):
    return {"version": 2, "frames": frames or [complete_frame()]}


def with_ground(frame):
    """The frame under test, plus a planted frame so the cycle has ground contact."""
    return complete_trace([frame, complete_frame(name="grounded", cell=(80, 0, 80, 80))])


class PngSizeTest(unittest.TestCase):
    def test_the_retained_strip_measures_the_same_as_pillow_reports(self):
        self.assertEqual(png_size(STRIP), SHEET)

    def test_a_file_that_is_not_a_png_fails_rather_than_returning_a_guess(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "not.png"
            path.write_bytes(b"GIF89a" + b"\0" * 40)
            with self.assertRaises(ValueError):
                png_size(path)
            short = Path(directory) / "short.png"
            short.write_bytes(b"\x89PNG\r\n\x1a\n")
            with self.assertRaises(ValueError):
                png_size(short)

    def test_a_png_declaring_no_pixels_fails(self):
        import struct

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.png"
            path.write_bytes(b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" +
                             struct.pack(">II", 0, 10))
            with self.assertRaises(ValueError):
                png_size(path)


class TraceCliTest(unittest.TestCase):
    def run_trace(self, path):
        return subprocess.run([sys.executable, str(ROOT / "scripts/trace_sprite_sheet.py"),
                               "--sheet", str(STRIP), "--trace", str(path)],
                              capture_output=True, text=True, timeout=10)

    def test_valid_trace_exits_successfully_without_changing_files(self):
        before = TRACE.read_bytes(), STRIP.read_bytes()
        result = self.run_trace(TRACE)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Validated 10 frames", result.stdout)
        self.assertEqual((TRACE.read_bytes(), STRIP.read_bytes()), before)

    def test_invalid_trace_returns_named_error_without_repairing_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.json"
            trace = complete_trace()
            del trace["frames"][0]["pose"]["knee_l"]
            path.write_text(json.dumps(trace))
            before = path.read_bytes()
            result = self.run_trace(path)
            self.assertEqual(result.returncode, 2)
            self.assertIn("still unplaced: knee_l", result.stderr)
            self.assertEqual(path.read_bytes(), before)


class ValidateTest(unittest.TestCase):
    def test_a_complete_frame_passes(self):
        self.assertIsNotNone(validate(complete_trace(), SHEET))

    def test_an_unplaced_joint_is_refused_and_named(self):
        trace = complete_trace()
        del trace["frames"][0]["pose"]["knee_r"]
        with self.assertRaises(ValueError) as caught:
            validate(trace, SHEET)
        self.assertIn("knee_r", str(caught.exception))

    def test_an_unmarked_joint_is_refused(self):
        trace = complete_trace()
        del trace["frames"][0]["confidence"]["hip_c"]
        with self.assertRaises(ValueError) as caught:
            validate(trace, SHEET)
        self.assertIn("observed or estimated", str(caught.exception))

    def test_a_nonsense_confidence_value_is_refused(self):
        trace = complete_trace()
        trace["frames"][0]["confidence"]["hip_c"] = "probably"
        with self.assertRaises(ValueError):
            validate(trace, SHEET)

    def test_a_joint_outside_its_own_cell_is_refused(self):
        for point in [[80, 10], [10, 80], [-1, 10], [10, -1]]:
            trace = complete_trace()
            trace["frames"][0]["pose"]["toe_l"] = point
            with self.assertRaises(ValueError) as caught:
                validate(trace, SHEET)
            self.assertIn("toe_l", str(caught.exception))

    def test_a_cell_off_the_sheet_is_refused(self):
        trace = complete_trace([complete_frame(cell=(760, 0, 80, 80))])
        with self.assertRaises(ValueError) as caught:
            validate(trace, SHEET)
        self.assertIn("outside", str(caught.exception))

    def test_an_unknown_joint_is_refused_rather_than_ignored(self):
        trace = complete_trace()
        trace["frames"][0]["pose"]["elbow_middle"] = [4, 4]
        with self.assertRaises(ValueError) as caught:
            validate(trace, SHEET)
        self.assertIn("elbow_middle", str(caught.exception))

    def test_duplicate_frame_names_are_refused(self):
        trace = complete_trace([complete_frame(), complete_frame(cell=(80, 0, 80, 80))])
        with self.assertRaises(ValueError):
            validate(trace, SHEET)

    def test_an_unknown_support_value_is_refused(self):
        trace = complete_trace([complete_frame(support="left")])
        with self.assertRaises(ValueError):
            validate(trace, SHEET)

    def test_a_cycle_with_no_ground_contact_is_refused(self):
        trace = complete_trace([complete_frame(support="flight"),
                                complete_frame(name="reference_02", cell=(80, 0, 80, 80),
                                               support="flight")])
        with self.assertRaises(ValueError) as caught:
            validate(trace, SHEET)
        self.assertIn("ground", str(caught.exception))

    def test_naming_the_raised_foot_as_support_is_refused(self):
        frame = complete_frame(support="far")
        frame["pose"]["ankle_l"], frame["pose"]["toe_l"] = [30, 60], [34, 65]
        frame["pose"]["ankle_r"], frame["pose"]["toe_r"] = [44, 50], [48, 55]
        with self.assertRaises(ValueError) as caught:
            validate(complete_trace([frame]), SHEET)
        self.assertIn("flip the support or swap the legs", str(caught.exception))

    def test_naming_the_planted_foot_as_support_passes(self):
        frame = complete_frame(support="near")
        frame["pose"]["ankle_l"], frame["pose"]["toe_l"] = [30, 60], [34, 65]
        frame["pose"]["ankle_r"], frame["pose"]["toe_r"] = [44, 50], [48, 55]
        self.assertIsNotNone(validate(complete_trace([frame]), SHEET))

    def test_a_heel_strike_is_not_mistaken_for_a_mislabelled_support(self):
        # The planted foot contacts at the heel with its toe raised above the
        # swinging foot's toe. Comparing toes alone would reject this wrongly.
        frame = complete_frame(support="near")
        frame["pose"]["ankle_l"], frame["pose"]["toe_l"] = [30, 66], [36, 58]
        frame["pose"]["ankle_r"], frame["pose"]["toe_r"] = [48, 52], [52, 60]
        self.assertIsNotNone(validate(complete_trace([frame]), SHEET))

    def test_a_flight_frame_is_exempt_from_the_support_check(self):
        frame = complete_frame(support="flight")
        frame["pose"]["ankle_l"], frame["pose"]["toe_l"] = [30, 60], [34, 65]
        frame["pose"]["ankle_r"], frame["pose"]["toe_r"] = [44, 50], [48, 55]
        self.assertIsNotNone(validate(with_ground(frame), SHEET))

    def test_an_empty_trace_is_refused(self):
        for bad in [{}, {"frames": []}, {"frames": "ten"}, []]:
            with self.assertRaises(ValueError):
                validate(bad, SHEET)

    def test_a_zero_sized_cell_is_refused(self):
        trace = complete_trace([complete_frame(cell=(0, 0, 0, 80))])
        with self.assertRaises(ValueError):
            validate(trace, SHEET)

    def test_non_numeric_coordinates_are_refused(self):
        trace = complete_trace()
        trace["frames"][0]["pose"]["neck"] = ["40", 30]
        with self.assertRaises(ValueError):
            validate(trace, SHEET)


class RetainedTraceTest(unittest.TestCase):
    def setUp(self):
        self.trace = json.loads(TRACE.read_text())

    def filled(self):
        trace = copy.deepcopy(self.trace)
        for frame in trace["frames"]:
            marked = frame.get("confidence", {})
            frame["confidence"] = {joint: marked.get(joint, "estimated") for joint in JOINTS}
        return trace

    def test_the_retained_trace_is_accepted(self):
        self.assertIsNotNone(validate(self.filled(), tuple(self.trace["source_size"])))

    def test_no_frame_names_a_raised_foot_as_its_support(self):
        conflicts = [support_conflict(frame) for frame in self.filled()["frames"]]
        self.assertEqual([message for message in conflicts if message], [])

    def test_the_named_support_foot_alternates_in_runs_around_the_cycle(self):
        planted = [frame["support"] for frame in self.trace["frames"]
                   if frame["support"] != "flight"]
        self.assertIn("near", planted)
        self.assertIn("far", planted)

    def test_every_joint_carries_a_confidence_so_none_passes_for_measured(self):
        for frame in self.trace["frames"]:
            marked = frame.get("confidence", {})
            self.assertEqual(sorted(marked), sorted(JOINTS), frame["name"])
            for joint, value in marked.items():
                self.assertIn(value, {"observed", "estimated"}, f"{frame['name']}/{joint}")


if __name__ == "__main__":
    unittest.main()
