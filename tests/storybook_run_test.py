"""Platform-neutral regression checks against the actual Blender run/export."""

import hashlib
import json
import math
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

from experiments.storybook_mouse import run_motion


ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT/'experiments/mouse_3d/storybook-run-v1/result'
NEUTRAL = ROOT/'experiments/mouse_3d/storybook-neutral-v1'


class StorybookRunTest(unittest.TestCase):
    asset = ASSET
    motion = run_motion

    @classmethod
    def setUpClass(cls):
        cls.report = json.loads((cls.asset/'motion.json').read_text())
        cls.review = json.loads((cls.asset/'review.json').read_text())

    def test_accepted_bundle_and_source_data_are_unchanged(self):
        acceptance = json.loads((NEUTRAL/'acceptance.json').read_text())
        for name, expected in acceptance['sha256'].items():
            self.assertEqual(hashlib.sha256((NEUTRAL/name).read_bytes()).hexdigest(), expected, name)
        self.assertEqual(self.report['before'], self.report['after'])
        self.assertEqual(self.report['before']['vertices'], 39998)
        self.assertEqual(self.report['before']['triangles'], 80000)
        self.assertTrue(self.report['before']['packed_images'])
        self.assertEqual(self.report['before']['linked_libraries'], 0)

    def test_saved_action_keeps_lengths_and_limb_attachments(self):
        for check in self.report['checks']:
            for name, points in check['bones'].items():
                self.assertLess(abs(math.dist(*points)-self.report['rest_bones'][name]['length']), 2e-5)
            for side in ('left', 'right'):
                for parent, child in (('thigh', 'shin'), ('shin', 'foot'),
                                      ('upper_arm', 'forearm'), ('forearm', 'hand')):
                    self.assertLess(math.dist(check['bones'][f'{parent}.{side}'][1],
                                              check['bones'][f'{child}.{side}'][0]), 2e-5)

    def test_reopened_blender_file_matches_motion_and_contact_markers(self):
        saved = json.loads((self.asset/'saved-scene-check.json').read_text())
        self.assertTrue(saved['reopened_saved_blend'])
        self.assertTrue(saved['source_data_unchanged'])
        self.assertTrue(saved['contact_markers_parented_to_foot_bones'])
        self.assertEqual(saved['samples_checked'], 193)
        for key in ('max_joint_reload_error', 'max_contact_marker_error', 'max_weight_sum_error',
                    'max_bone_scale_error', 'max_head_rotation_difference'):
            self.assertLess(saved[key], 2e-5)
        self.assertEqual(hashlib.sha256((self.asset/'run-mouse.blend').read_bytes()).hexdigest(), saved['blend_sha256'])

    def test_actual_support_holds_ground_and_does_not_slide(self):
        # 0.001 scene unit is below a tenth of one 512px pixel. Dense FK keys
        # approximate analytic stance between keys; this bounds that error.
        tolerance = .001
        checks = self.report['checks']
        for check in checks:
            self.assertGreaterEqual(check['lowest_z'], -tolerance)
            for paw_check in check['paws'].values():
                if paw_check['stance']:
                    self.assertLess(abs(paw_check['lowest_z']), tolerance)
        for side in ('left', 'right'):
            for previous, current in zip(checks, checks[1:]):
                a, b = previous['paws'][side], current['paws'][side]
                if not a['stance'] or not b['stance'] or b['phase'] < a['phase']:
                    continue
                delta_t = (current['phase']-previous['phase'])*self.motion.CYCLE_SECONDS
                # Ground-space position includes the declared -Y root travel.
                marker = self.report.get('contact_marker', 'sole')
                delta = np.array(b['markers'][marker])-a['markers'][marker]
                delta[1] -= self.motion.SPEED*delta_t
                self.assertLess(float(np.linalg.norm(delta)), tolerance)

    def test_recovery_clears_ground_and_both_flight_phases_exist(self):
        checks = self.report['checks']
        flights = [c for c in checks if all(not p['stance'] for p in c['paws'].values())]
        self.assertTrue(any(0 < c['phase'] < .5 for c in flights))
        self.assertTrue(any(.5 < c['phase'] < 1 for c in flights))
        for side in ('left', 'right'):
            high = [p for c in checks if (p := c['paws'][side])['phase'] > .50 and p['phase'] < .75]
            self.assertGreater(min(p['lowest_z'] for p in high), .35)

    def test_cycle_is_closed_and_paw_paths_have_continuous_boundaries(self):
        first, last = self.report['checks'][0], self.report['checks'][-1]
        for name in first['bones']:
            np.testing.assert_allclose(first['bones'][name], last['bones'][name], atol=1e-6)
        for boundary in (0, self.motion.STANCE_END):
            left, right = self.motion.paw(boundary-1e-6), self.motion.paw(boundary+1e-6)
            for coordinate in ('y', 'clearance', 'pitch'):
                self.assertLess(abs(left[coordinate]-right[coordinate]), 1e-4)

    def test_decoded_pngs_have_fixed_cells_origin_and_transparent_margins(self):
        self.assertEqual(len(self.review['frames']), 24)
        self.assertEqual([f['index'] for f in self.review['frames']], list(range(24)))
        origins = [tuple(f['origin_512']) for f in self.review['frames']]
        self.assertEqual(len(set(origins)), 1)
        for size in (96, 128, 512):
            with Image.open(self.asset/f'sprites-{size}.png') as packed, Image.open(self.asset/f'sprites-{size}-12.png') as sparse:
                self.assertEqual(packed.size, (6*size, 4*size))
                self.assertEqual(sparse.size, (6*size, 2*size))
                for index in range(24):
                    with Image.open(self.asset/f'frames-{size}'/f'run-{index:02}.png') as frame:
                        self.assertEqual(frame.mode, 'RGBA')
                        self.assertEqual(frame.size, (size, size))
                        box = frame.getchannel('A').point(lambda v: 255 if v >= 128 else 0).getbbox()
                        self.assertIsNotNone(box)
                        self.assertGreater(min(box[:2]), 0)
                        self.assertLess(max(box[2:]), size)
                        x, y = index%6*size, index//6*size
                        np.testing.assert_array_equal(frame, packed.crop((x, y, x+size, y+size)))
                        if index%2 == 0:
                            cell = index//2
                            x, y = cell%6*size, cell//6*size
                            np.testing.assert_array_equal(frame, sparse.crop((x, y, x+size, y+size)))

    def test_animation_decodes_with_same_duration_and_frame_content(self):
        for size in (96, 128):
            for count in (12, 24):
                with Image.open(self.asset/f'run-{size}-{count}.apng') as animation:
                    self.assertEqual(animation.n_frames, count)
                    self.assertEqual(animation.info['loop'], 0)
                    duration = 0
                    for index in range(count):
                        animation.seek(index)
                        duration += animation.info['duration']
                        with Image.open(self.asset/f'frames-{size}'/f'run-{index*24//count:02}.png') as frame:
                            np.testing.assert_array_equal(animation.convert('RGBA'), frame)
                    self.assertLess(abs(duration-1000*self.motion.CYCLE_SECONDS), 1)

    def test_retained_artifact_hashes(self):
        for name, expected in self.review['sha256'].items():
            self.assertEqual(hashlib.sha256((self.asset/name).read_bytes()).hexdigest(), expected, name)


if __name__ == '__main__':
    unittest.main()
