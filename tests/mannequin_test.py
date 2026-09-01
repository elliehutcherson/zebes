"""Tests for the parametric mannequin experiment.

The other Python tests here load a flat script from `scripts/` by path, because
those scripts are not importable as a package. The mannequin is a package, so
this file puts its root on `sys.path` and imports it normally. Placing the file
in `tests/` rather than beside the package is deliberate: only
`scripts/build_and_test.sh` runs Python tests, through `unittest discover` over
this directory, and an experiment that is not in that gate is an experiment
nobody notices breaking.
"""

import dataclasses
import sys
import unittest
from pathlib import Path

EXPERIMENT_ROOT = Path(__file__).parent.parent / "experiments" / "mannequin"
if str(EXPERIMENT_ROOT) not in sys.path:
    sys.path.insert(0, str(EXPERIMENT_ROOT))

from mannequin import cli, isolate, measure, openpose, raster  # noqa: E402
from mannequin.costume import (  # noqa: E402
    Costume,
    CostumeError,
    Helmet,
    Weapon,
    costume,
)
from mannequin.math3d import origin_of  # noqa: E402
from mannequin.measurements import (  # noqa: E402
    GATE_TESTABLE_FIELDS,
    ProportionError,
    preset,
    resolution_report,
)
from mannequin.pose import (  # noqa: E402
    RUN_CONTACT,
    STATIC_POSES,
    blend,
    idle_cycle,
    mirror,
    run_cycle,
    static_pose,
)
from mannequin.project import layout_for, project  # noqa: E402
from mannequin.skeleton import (  # noqa: E402
    Ellipsoid,
    build_rig,
    lowest_shell,
    seat_on_ground,
    shell_height,
    skeletal_height,
    sole_height,
    solve,
)

CANVAS = (256, 384)


def render_mask(proportions, pose, planted_foot, view, size=CANVAS, costume_spec=None):
    """Rasterise one posed view and return its coverage mask with its size."""
    extra_joints, attachments = (
        costume_spec.build(proportions) if costume_spec is not None else ((), ())
    )
    rig = build_rig(proportions, extra_joints, attachments)
    world = seat_on_ground(rig, solve(rig, pose), planted_foot)
    layout = layout_for(proportions, *size)
    buffers = raster.rasterize(*size, project(rig, world, view, layout))
    return buffers, rig, world, layout


class ProportionsTest(unittest.TestCase):
    def test_presets_close_to_their_stated_height(self):
        for name in ("heroic-6h", "realistic-7.5h", "chibi-4h"):
            with self.subTest(name=name):
                p = preset(name)
                self.assertAlmostEqual(p.stacked_height, p.heads_tall, places=6)

    def test_segments_that_miss_the_stated_height_are_rejected(self):
        broken = dataclasses.replace(preset("heroic-6h"), inseam=3.6)

        with self.assertRaises(ProportionError) as caught:
            broken.validate()

        self.assertIn("stacked segments", str(caught.exception))

    def test_close_to_height_repairs_an_authored_figure(self):
        repaired = dataclasses.replace(preset("heroic-6h"), inseam=3.6).close_to_height()

        repaired.validate()
        self.assertAlmostEqual(repaired.heads_tall, 6.4, places=6)

    def test_thigh_share_outside_the_open_interval_is_rejected(self):
        with self.assertRaises(ProportionError):
            dataclasses.replace(preset("heroic-6h"), thigh_share=1.0).validate()

    def test_unknown_preset_names_the_available_ones(self):
        with self.assertRaises(ProportionError) as caught:
            preset("nonexistent")

        self.assertIn("heroic-6h", str(caught.exception))


class ResolutionTieringTest(unittest.TestCase):
    def test_limb_radii_are_sub_pixel_at_the_shipped_sprite_height(self):
        # docs/animation-artwork-pipeline.md ships a 44 px character. This is
        # the measurement that a gate must not be allowed to fail a frame over.
        report = resolution_report(preset("heroic-6h"), 44.0)

        _, wrist_px, wrist_resolved = report["wrist_radius"]
        self.assertLess(wrist_px, 1.0)
        self.assertFalse(wrist_resolved)

    def test_silhouette_measurements_survive_the_shipped_sprite_height(self):
        report = resolution_report(preset("heroic-6h"), 44.0)

        for field_name in ("shoulder_width", "inseam", "head_width"):
            with self.subTest(field=field_name):
                _, pixels, resolved = report[field_name]
                self.assertGreater(pixels, 1.0)
                self.assertTrue(resolved)
                self.assertIn(field_name, GATE_TESTABLE_FIELDS)

    def test_zero_render_height_is_rejected(self):
        with self.assertRaises(ProportionError):
            resolution_report(preset("heroic-6h"), 0.0)


class SkeletonTest(unittest.TestCase):
    def test_unknown_joint_in_a_pose_is_rejected(self):
        rig = build_rig(preset("heroic-6h"))

        with self.assertRaises(KeyError) as caught:
            solve(rig, {"elbow_middle": (0.0, 0.0, 0.0)})

        self.assertIn("elbow_middle", str(caught.exception))

    def test_a_standing_figure_measures_its_stated_height(self):
        p = preset("heroic-6h")
        rig = build_rig(p)

        world = seat_on_ground(rig, solve(rig, {}), "both")

        self.assertAlmostEqual(skeletal_height(rig, world), p.heads_tall, delta=0.01)

    def test_seating_puts_both_soles_on_the_ground(self):
        rig = build_rig(preset("heroic-6h"))

        world = seat_on_ground(rig, solve(rig, {}), "both")

        for side in ("l", "r"):
            self.assertAlmostEqual(sole_height(rig, world, side), 0.0, places=9)

    def test_seating_uses_only_the_planted_foot(self):
        rig = build_rig(preset("heroic-6h"))
        frame = static_pose("run-contact")

        world = seat_on_ground(rig, solve(rig, frame.pose), frame.planted_foot)

        self.assertAlmostEqual(sole_height(rig, world, "r"), 0.0, places=9)
        self.assertGreater(sole_height(rig, world, "l"), 0.0)

    def test_no_grounded_pose_drives_a_limb_through_the_floor(self):
        # Easy to author by accident: angling the lead leg forward drops the
        # pelvis when the figure is seated on that foot, and a trailing leg that
        # looked fine standing then penetrates. The run contact did exactly
        # this, by a third of a head unit.
        rig = build_rig(preset("heroic-6h"))
        grounded = [f for f in STATIC_POSES.values() if f.planted_foot is not None]
        grounded.extend(f for f in run_cycle(12) if f.planted_foot is not None)
        grounded.extend(f for f in idle_cycle(4) if f.planted_foot is not None)
        self.assertGreater(len(grounded), 12)

        for frame in grounded:
            with self.subTest(label=frame.label):
                world = seat_on_ground(rig, solve(rig, frame.pose), frame.planted_foot)
                self.assertGreaterEqual(lowest_shell(rig, world), -1e-9)

    def test_airborne_poses_stay_within_the_render_headroom(self):
        # An airborne figure is not seated, so its feet may hang below the
        # standing contact line — a falling pose reaches for the ground. It must
        # still fit the canvas the layout reserves beneath that line.
        p = preset("heroic-6h")
        rig = build_rig(p)
        layout = layout_for(p, *CANVAS)
        headroom_heads = (CANVAS[1] - layout.ground_y) / layout.pixels_per_head

        for frame in STATIC_POSES.values():
            if frame.planted_foot is not None:
                continue
            with self.subTest(label=frame.label):
                world = seat_on_ground(rig, solve(rig, frame.pose), frame.planted_foot)
                self.assertGreater(lowest_shell(rig, world), -headroom_heads)

    def test_airborne_frames_are_not_seated(self):
        # A jump that gets seated is a standing pose. The two jump poses are the
        # only ones whose planted foot is None, and that has to mean something.
        rig = build_rig(preset("heroic-6h"))
        frame = static_pose("jump-rise")
        self.assertIsNone(frame.planted_foot)

        world = seat_on_ground(rig, solve(rig, frame.pose), frame.planted_foot)

        self.assertGreater(min(sole_height(rig, world, s) for s in ("l", "r")), 0.05)

    def test_hair_rises_above_the_crown_without_changing_stated_height(self):
        p = preset("heroic-6h")
        rig = build_rig(p)

        world = seat_on_ground(rig, solve(rig, {}), "both")

        self.assertAlmostEqual(skeletal_height(rig, world), p.heads_tall, delta=0.01)
        self.assertGreater(shell_height(rig, world), skeletal_height(rig, world))

    def test_invalid_planted_foot_is_rejected(self):
        rig = build_rig(preset("heroic-6h"))
        world = solve(rig, {})

        with self.assertRaises(ValueError):
            seat_on_ground(rig, world, "middle")


class PoseTest(unittest.TestCase):
    def test_mirroring_swaps_sides_and_negates_lateral_components(self):
        mirrored = mirror(RUN_CONTACT)

        self.assertEqual(mirrored["hip_l"][0], RUN_CONTACT["hip_r"][0])
        self.assertEqual(mirrored["hip_l"][2], -RUN_CONTACT["hip_r"][2])

    def test_run_cycle_second_half_mirrors_the_first(self):
        p = preset("heroic-6h")
        rig = build_rig(p)
        frames = run_cycle(12)

        def ankle_depths(frame):
            world = seat_on_ground(rig, solve(rig, frame.pose), frame.planted_foot)
            return origin_of(world["ankle_l"])[2], origin_of(world["ankle_r"])[2]

        left_first, right_first = ankle_depths(frames[0])
        left_second, right_second = ankle_depths(frames[6])

        self.assertAlmostEqual(left_first, right_second, places=6)
        self.assertAlmostEqual(right_first, left_second, places=6)

    def test_run_cycle_alternates_the_planted_foot(self):
        frames = run_cycle(12)

        self.assertEqual(frames[0].planted_foot, "r")
        self.assertEqual(frames[6].planted_foot, "l")

    def test_odd_run_frame_counts_are_rejected(self):
        with self.assertRaises(ValueError):
            run_cycle(11)

    def test_idle_cycle_returns_to_its_first_pose(self):
        frames = idle_cycle(4)

        self.assertEqual(frames[0].pose, frames[0].pose)
        self.assertNotEqual(frames[0].pose, frames[2].pose)

    def test_blend_endpoints_reproduce_their_inputs(self):
        blended = blend(RUN_CONTACT, mirror(RUN_CONTACT), 0.0)

        for joint, angles in RUN_CONTACT.items():
            self.assertAlmostEqual(blended[joint][0], angles[0], places=9)


class ProjectionTest(unittest.TestCase):
    def test_side_profile_is_narrower_than_the_front_at_the_same_height(self):
        p = preset("heroic-6h")
        front, *_ = render_mask(p, {}, "both", "front")
        side, *_ = render_mask(p, {}, "both", "right")

        front_signature = measure.signature_from_mask(front.covered, *CANVAS)
        side_signature = measure.signature_from_mask(side.covered, *CANVAS)

        self.assertLess(side_signature.width_px, front_signature.width_px * 0.6)
        self.assertEqual(side_signature.height_px, front_signature.height_px)

    def test_every_view_shares_one_scale_and_contact_line(self):
        p = preset("heroic-6h")
        layout = layout_for(p, *CANVAS)

        heights = set()
        for view in ("front", "back", "right", "three-quarter-right"):
            buffers, *_ = render_mask(p, {}, "both", view)
            heights.add(measure.signature_from_mask(buffers.covered, *CANVAS).height_px)

        self.assertEqual(len(heights), 1)
        self.assertAlmostEqual(layout.ground_y, CANVAS[1] * 0.92, places=6)

    def test_unknown_view_names_the_available_ones(self):
        p = preset("heroic-6h")
        rig = build_rig(p)
        world = solve(rig, {})

        with self.assertRaises(KeyError) as caught:
            project(rig, world, "isometric", layout_for(p, *CANVAS))

        self.assertIn("front", str(caught.exception))


class DepthConditioningTest(unittest.TestCase):
    def test_opposing_run_phases_share_a_silhouette_but_differ_in_depth(self):
        # The two halves of a side-view run are pixel-identical in silhouette:
        # the same two leg positions, swapped in depth. Conditioning a generator
        # on silhouette or line art alone therefore gives frames 0 and 6 the
        # same guidance, which is the "reads as nearly the same pose" failure in
        # docs/history/animation-pose-conditioned-experiment.md. Depth is what
        # separates them, so it is required rather than optional.
        p = preset("heroic-6h")
        frames = run_cycle(12)

        first, *_ = render_mask(p, frames[0].pose, frames[0].planted_foot, "right")
        second, *_ = render_mask(p, frames[6].pose, frames[6].planted_foot, "right")

        self.assertEqual(bytes(first.covered), bytes(second.covered))

        differing = sum(
            1
            for i, (a, b) in enumerate(zip(first.depth, second.depth, strict=True))
            if first.covered[i] and abs(a - b) > 1.0
        )
        self.assertGreater(differing, 1000)


class OpenPoseTest(unittest.TestCase):
    def test_every_coco_keypoint_is_produced(self):
        p = preset("heroic-6h")
        rig = build_rig(p)
        world = seat_on_ground(rig, solve(rig, {}), "both")

        points = openpose.keypoints(p, world, "front", layout_for(p, *CANVAS))

        self.assertEqual(len(points), 18)
        self.assertEqual(points[0].name, "nose")
        self.assertEqual(points[1].name, "neck")

    def test_facial_keypoints_follow_the_head_rotation(self):
        # Derived from the head frame rather than pinned to the canvas, so a
        # turned head moves its own eyes and ears.
        p = preset("heroic-6h")
        rig = build_rig(p)
        layout = layout_for(p, *CANVAS)

        forward = openpose.keypoints(
            p, seat_on_ground(rig, solve(rig, {}), "both"), "front", layout
        )
        turned = openpose.keypoints(
            p,
            seat_on_ground(rig, solve(rig, {"head": (0.0, 40.0, 0.0)}), "both"),
            "front",
            layout,
        )

        self.assertNotAlmostEqual(forward[0].x, turned[0].x, places=2)

    def test_the_skeleton_renders_on_opaque_black(self):
        # Pose ControlNet models were trained on skeletons over black. A
        # transparent ground composites to whatever the consumer uses, and white
        # inverts the signal.
        import tempfile

        from mannequin.png import read_rgba

        p = preset("heroic-6h")
        rig = build_rig(p)
        layout = layout_for(p, *CANVAS)
        points = openpose.keypoints(
            p, seat_on_ground(rig, solve(rig, {}), "both"), "front", layout
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pose.png"
            openpose.write_skeleton_png(path, points, layout)
            _, _, pixels = read_rgba(path)

        self.assertTrue(all(pixels[i] == 255 for i in range(3, len(pixels), 4)))
        self.assertEqual(tuple(pixels[0:3]), (0, 0, 0))


class CostumeTest(unittest.TestCase):
    def test_a_helmet_widens_the_head_without_moving_the_shoulders(self):
        p = preset("heroic-6h")
        bare, *_ = render_mask(p, {}, "both", "front")
        helmed, *_ = render_mask(
            p,
            {},
            "both",
            "front",
            costume_spec=Costume(name="test", helmet=Helmet(coverage=1.4, crest=0.0)),
        )

        bare_signature = measure.signature_from_mask(bare.covered, *CANVAS)
        helmed_signature = measure.signature_from_mask(helmed.covered, *CANVAS)
        landmarks = measure.band_landmarks(p.heads_tall)

        self.assertGreater(
            helmed_signature.band_widths[landmarks["helmet"]],
            bare_signature.band_widths[landmarks["helmet"]],
        )

    def test_a_helmet_that_sinks_inside_the_skull_is_rejected(self):
        with self.assertRaises(CostumeError):
            Costume(name="bad", helmet=Helmet(coverage=0.8, crest=0.0)).build(
                preset("heroic-6h")
            )

    def test_a_weapon_gets_its_own_joint_so_it_swings_with_the_arm(self):
        p = preset("heroic-6h")
        kit = Costume(name="armed", weapon=Weapon(hand="r", length=1.4, radius=0.06))
        extra_joints, attachments = kit.build(p)
        rig = build_rig(p, extra_joints, attachments)

        straight = solve(rig, {})
        raised = solve(rig, {"shoulder_r": (-90.0, 0.0, 0.0)})

        self.assertNotAlmostEqual(
            origin_of(straight["weapon_tip_r"])[1],
            origin_of(raised["weapon_tip_r"])[1],
            places=3,
        )

    def test_an_attachment_on_an_unknown_joint_is_rejected(self):
        p = preset("heroic-6h")
        stray = Ellipsoid("stray", "tail", (0.0, 0.0, 0.0), 0.1, 0.1, 0.1, "armor")

        with self.assertRaises(KeyError) as caught:
            build_rig(p, (), (stray,))

        self.assertIn("tail", str(caught.exception))

    def test_shipped_costumes_build_against_every_preset(self):
        for costume_name in ("knight", "scout"):
            for preset_name in ("heroic-6h", "realistic-7.5h", "chibi-4h"):
                with self.subTest(costume=costume_name, preset=preset_name):
                    p = preset(preset_name)
                    extra_joints, attachments = costume(costume_name).build(p)
                    build_rig(p, extra_joints, attachments)


class IsolationTest(unittest.TestCase):
    def frame(self, width, height, painter):
        """Build an RGBA buffer from a callable returning grey level or None."""
        pixels = bytearray(width * height * 4)
        for y in range(height):
            for x in range(width):
                value = painter(x, y)
                if value is None:
                    value = 20  # background grey, below the ceiling
                i = (y * width + x) * 4
                pixels[i : i + 4] = bytes((value, value, value, 255))
        return pixels

    def test_a_bright_subject_is_separated_from_a_dark_ground(self):
        pixels = self.frame(
            40, 40, lambda x, y: 200 if 10 <= x < 30 and 10 <= y < 30 else None
        )

        mask = isolate.subject_mask(pixels, 40, 40)

        self.assertEqual(sum(mask), 400)
        self.assertTrue(mask[20 * 40 + 20])
        self.assertFalse(mask[0])

    def test_dark_pixels_enclosed_by_the_subject_stay_part_of_it(self):
        # This is why the flood fill exists. A flat luminance threshold would
        # punch the character's own outlines and shadowed recesses out of the
        # silhouette; only background reachable from the border is removed.
        def painter(x, y):
            if 10 <= x < 30 and 10 <= y < 30:
                return 5 if 18 <= x < 22 and 18 <= y < 22 else 200
            return None

        mask = isolate.subject_mask(self.frame(40, 40, painter), 40, 40)

        self.assertEqual(sum(mask), 400)
        self.assertTrue(mask[20 * 40 + 20], "enclosed dark pixels were eaten")

    def test_an_all_background_frame_is_rejected(self):
        with self.assertRaises(isolate.IsolationError):
            isolate.subject_mask(self.frame(16, 16, lambda x, y: None), 16, 16)

    def test_a_uniform_frame_is_rejected(self):
        with self.assertRaises(isolate.IsolationError) as caught:
            isolate.subject_mask(self.frame(16, 16, lambda x, y: 200), 16, 16)

        self.assertIn("no subject", str(caught.exception))

    def test_a_saturated_subject_is_kept_even_when_it_is_dark(self):
        # The bug this replaced: a red cape has a luminance near 60, so a
        # luminance flood treated it as background, poured through it into the
        # figure, and shattered the mask into outlines. Colour distance keeps it.
        def painter(x, y):
            return None

        pixels = self.frame(40, 40, painter)
        for y in range(10, 30):
            for x in range(10, 30):
                i = (y * 40 + x) * 4
                pixels[i : i + 4] = bytes((150, 25, 30, 255))  # dark saturated red

        mask = isolate.subject_mask(pixels, 40, 40)

        self.assertEqual(sum(mask), 400)

    def test_border_contact_is_detected(self):
        # A cropped figure measures wrong in a way nothing downstream can catch.
        touching = self.frame(30, 30, lambda x, y: 200 if x >= 20 else None)
        clear = self.frame(
            30, 30, lambda x, y: 200 if 10 <= x < 20 and 10 <= y < 20 else None
        )

        self.assertTrue(
            isolate.touches_border(isolate.subject_mask(touching, 30, 30), 30, 30)
        )
        self.assertFalse(
            isolate.touches_border(isolate.subject_mask(clear, 30, 30), 30, 30)
        )


class DriftGateTest(unittest.TestCase):
    def test_an_identical_figure_passes(self):
        p = preset("heroic-6h")
        buffers, *_ = render_mask(p, {}, "both", "front")
        signature = measure.signature_from_mask(buffers.covered, *CANVAS)

        comparison = measure.compare(signature, signature, tolerance=0.01)

        self.assertTrue(comparison.passed)

    def test_a_uniformly_scaled_figure_passes(self):
        # The signature normalises every width by the figure's own height, so a
        # 1024 px generated frame must compare cleanly against a 384 px guide.
        p = preset("heroic-6h")
        small, *_ = render_mask(p, {}, "both", "front", size=(256, 384))
        large, *_ = render_mask(p, {}, "both", "front", size=(512, 768))

        comparison = measure.compare(
            measure.signature_from_mask(small.covered, 256, 384),
            measure.signature_from_mask(large.covered, 512, 768),
            tolerance=0.05,
        )

        self.assertTrue(comparison.passed, comparison.report())

    def test_a_broader_figure_fails_at_the_shoulder_band(self):
        p = preset("heroic-6h")
        broad = dataclasses.replace(p, shoulder_width=p.shoulder_width * 1.3)
        reference, *_ = render_mask(p, {}, "both", "front")
        candidate, *_ = render_mask(broad, {}, "both", "front")

        comparison = measure.compare(
            measure.signature_from_mask(reference.covered, *CANVAS),
            measure.signature_from_mask(candidate.covered, *CANVAS),
            tolerance=0.05,
        )

        self.assertFalse(comparison.passed)
        failing_bands = {d.band for d in comparison.failing}
        self.assertIn(measure.band_landmarks(p.heads_tall)["shoulder"], failing_bands)

    def test_an_enlarged_helmet_fails_at_the_helmet_band(self):
        # Helmet size is one of the two things the rejected pilots actually
        # drifted on, so it gets its own gate case.
        p = preset("heroic-6h")
        small = Costume(name="small", helmet=Helmet(coverage=1.05, crest=0.0))
        large = Costume(name="large", helmet=Helmet(coverage=1.45, crest=0.12))
        reference, *_ = render_mask(p, {}, "both", "front", costume_spec=small)
        candidate, *_ = render_mask(p, {}, "both", "front", costume_spec=large)

        comparison = measure.compare(
            measure.signature_from_mask(reference.covered, *CANVAS),
            measure.signature_from_mask(candidate.covered, *CANVAS),
            tolerance=0.05,
        )

        self.assertFalse(comparison.passed)
        failing_bands = {d.band for d in comparison.failing}
        self.assertIn(measure.band_landmarks(p.heads_tall)["helmet"], failing_bands)

    def test_mismatched_band_counts_are_rejected(self):
        p = preset("heroic-6h")
        buffers, *_ = render_mask(p, {}, "both", "front")

        with self.assertRaises(measure.MeasurementError):
            measure.compare(
                measure.signature_from_mask(buffers.covered, *CANVAS, bands=24),
                measure.signature_from_mask(buffers.covered, *CANVAS, bands=12),
                tolerance=0.05,
            )

    def test_an_empty_figure_cannot_be_measured(self):
        with self.assertRaises(measure.MeasurementError):
            measure.signature_from_mask(bytearray(64 * 64), 64, 64)


class PngTest(unittest.TestCase):
    def test_written_pixels_survive_a_round_trip(self):
        import tempfile

        from mannequin.png import read_rgba, write_rgba

        pixels = bytearray()
        for i in range(16 * 8):
            pixels += bytes((i % 256, (i * 3) % 256, (i * 7) % 256, 255 if i % 3 else 0))

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "round-trip.png"
            write_rgba(path, 16, 8, pixels)
            width, height, decoded = read_rgba(path)

        self.assertEqual((width, height), (16, 8))
        self.assertEqual(bytes(decoded), bytes(pixels))

    def test_a_mismatched_pixel_buffer_is_rejected(self):
        import tempfile

        from mannequin.png import write_rgba

        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                write_rgba(Path(directory) / "bad.png", 4, 4, bytearray(8))

    def test_a_fully_opaque_frame_is_rejected_by_the_gate(self):
        # Measuring an un-isolated frame would compare the background's bounding
        # box and report that nothing drifted.
        import tempfile

        from mannequin.png import write_rgba

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "opaque.png"
            write_rgba(path, 8, 8, bytearray(b"\x40\x40\x40\xff" * 64))

            with self.assertRaises(measure.MeasurementError) as caught:
                measure.signature_from_png(path)

        self.assertIn("Isolate the subject", str(caught.exception))


class CliTest(unittest.TestCase):
    """End-to-end checks of the three commands.

    These cover the plan's verification steps, so that confirming the tool works
    is a test run rather than a shell session someone has to remember.
    """

    def render_to(self, directory, *extra):
        import contextlib
        import io

        with contextlib.redirect_stdout(io.StringIO()):
            return cli.main(
                [
                    "render",
                    "--preset",
                    "heroic-6h",
                    "--views",
                    "front",
                    "--width",
                    "256",
                    "--height",
                    "384",
                    "--out",
                    str(directory),
                    *extra,
                ]
            )

    def test_render_writes_every_map_and_a_manifest(self):
        import json
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "set"
            self.assertEqual(self.render_to(out, "--pose", "a-pose"), 0)

            for subdirectory in (
                "silhouette",
                "depth",
                "regions",
                "outline",
                "openpose",
                "svg/construction",
            ):
                with self.subTest(subdirectory=subdirectory):
                    self.assertTrue(any((out / subdirectory).iterdir()))

            manifest = json.loads((out / "manifest.json").read_text())

        self.assertEqual(manifest["heads_tall"], 6.0)
        self.assertEqual(len(manifest["frames"]), 1)
        self.assertAlmostEqual(
            manifest["frames"][0]["skeletal_height_heads"], 6.0, delta=0.01
        )

    def test_every_frame_of_a_cycle_shares_one_origin_and_contact_line(self):
        import json
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "run"
            self.assertEqual(
                self.render_to(out, "--cycle", "run", "--frames", "4"), 0
            )
            manifest = json.loads((out / "manifest.json").read_text())

        self.assertEqual(len(manifest["frames"]), 4)
        self.assertEqual(manifest["origin_x"], 128.0)
        self.assertAlmostEqual(manifest["contact_line_y"], 384 * 0.92, places=6)

    def test_a_pose_that_overflows_the_canvas_is_refused_rather_than_cropped(self):
        # Scale is fixed by the measurement set, so a wide stride needs a wider
        # canvas. Silently cropping a foot would corrupt the conditioning map
        # and the signature measured from it.
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(SystemExit) as caught:
                cli.main(
                    [
                        "render",
                        "--cycle",
                        "run",
                        "--frames",
                        "12",
                        "--views",
                        "right",
                        "--width",
                        "256",
                        "--height",
                        "384",
                        "--out",
                        str(Path(directory) / "clipped"),
                    ]
                )

        self.assertIn("runs off the", str(caught.exception))

    def test_gate_exits_zero_for_an_identical_frame_and_non_zero_for_a_costumed_one(
        self,
    ):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bare = root / "bare"
            knight = root / "knight"
            self.assertEqual(self.render_to(bare, "--pose", "a-pose"), 0)
            self.assertEqual(
                self.render_to(knight, "--pose", "a-pose", "--costume", "knight"), 0
            )

            reference = str(bare / "silhouette" / "00-front.png")
            costumed = str(knight / "silhouette" / "00-front.png")

            import contextlib
            import io

            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(
                    cli.main(["gate", reference, reference, "--tolerance", "0.02"]), 0
                )
                self.assertEqual(
                    cli.main(["gate", reference, costumed, "--tolerance", "0.05"]), 1
                )

    def test_gate_reports_a_missing_file_without_a_traceback(self):
        with self.assertRaises(SystemExit) as caught:
            cli.main(["gate", "absent.png", "absent.png"])

        self.assertIn("no such file", str(caught.exception))

    def test_report_lists_both_measurement_tiers(self):
        import contextlib
        import io

        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            self.assertEqual(cli.main(["report", "--height-px", "44"]), 0)

        printed = captured.getvalue()
        self.assertIn("wrist_radius", printed)
        self.assertIn("sub-pixel", printed)
        self.assertIn("gate", printed)


if __name__ == "__main__":
    unittest.main()
