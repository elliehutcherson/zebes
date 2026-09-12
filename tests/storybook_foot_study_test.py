"""Preservation, local edit scope, saved-action mechanics and decoded exports."""

import hashlib
import json
import math
from pathlib import Path
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT/'experiments/mouse_3d/storybook-foot-study-v1'


class StorybookFootStudyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result = BUNDLE/'result'
        cls.study = json.loads((cls.result/'study.json').read_text())
        cls.review = json.loads((cls.result/'review.json').read_text())

    def test_master_and_both_runs_are_preserved_byte_for_byte(self):
        manifest = json.loads((BUNDLE/'preservation.json').read_text())
        self.assertEqual(set(manifest), {'storybook-neutral-v1', 'storybook-run-v1', 'storybook-run-v2'})
        for name, files in manifest.items():
            directory = BUNDLE.parent/name
            self.assertEqual(set(files), {str(p.relative_to(directory)) for p in directory.rglob('*') if p.is_file()})
            for path, expected in files.items():
                self.assertEqual(hashlib.sha256((directory/path).read_bytes()).hexdigest(), expected, f'{name}/{path}')

    def test_shape_and_binding_edits_are_local_and_topology_is_preserved(self):
        study = self.study
        region = set(study['region_vertex_indices'])
        for field in ('changed_vertex_indices', 'changed_weight_indices'):
            self.assertTrue(study[field])
            self.assertLessEqual(set(study[field]), region)
        self.assertLess(study['original_region_bounds'][1][2], .72)
        self.assertEqual(set(study['changed_bones']),
                         {f'{part}.{side}' for part in ('shin', 'foot', 'toe') for side in ('left', 'right')})
        self.assertEqual(study['triangles'], 80000)
        self.assertEqual(study['vertex_count'], 39998)
        self.assertTrue(study['topology_unchanged'])
        self.assertEqual(study['images']['Study_BaseColor'],
                         '52c29b7e8b532e2f51a48117b4c8b521ec73f9643170a7874e34d7de8a0a381e')
        self.assertLess(study['max_weight_sum_error'], 1e-6)
        self.assertTrue(study['saved_copy_verified'])
        self.assertEqual(*study['outside_region_sha256_before_after'])

    def test_saved_action_preserves_lengths_and_joint_continuity(self):
        self.assertEqual(len(self.study['checks']), 257)
        self.assertEqual(set(self.study['rest_bones']), set(self.study['original_bones'])|{'toe.left', 'toe.right'})
        for check in self.study['checks']:
            for name, (head, tail) in check['bones'].items():
                self.assertAlmostEqual(math.dist(head, tail), self.study['rest_bones'][name]['length'], delta=1e-5)
            for side in ('left', 'right'):
                for parent, child in (('thigh', 'shin'), ('shin', 'foot'), ('foot', 'toe')):
                    self.assertLess(math.dist(check['bones'][f'{parent}.{side}'][1],
                                              check['bones'][f'{child}.{side}'][0]), 1e-5)

    def test_toe_patch_stays_planted_while_hock_compresses(self):
        checks = {c['frame']: c for c in self.study['checks']}
        for side in ('left', 'right'):
            patch = checks[9]['toe_patch'][side]
            contact = [p for p in patch if p[2] < .02]
            self.assertGreater(len(contact), 10)
            self.assertGreater(max(p[0] for p in contact)-min(p[0] for p in contact), .15)
            for frame in (13, 17):
                current = checks[frame]['toe_patch'][side]
                self.assertEqual(len(patch), len(current))
                self.assertLess(max(math.dist(a, b) for a, b in zip(patch, current)), 1e-5)
        hock_z = lambda frame: checks[frame]['bones']['foot.left'][0][2]
        self.assertGreater(hock_z(9)-hock_z(17), .05)
        self.assertGreater(hock_z(25)-hock_z(17), .15)

    def test_toes_roll_and_recovery_folds_without_ground_penetration(self):
        for check in self.study['checks']:
            self.assertGreater(check['lowest_z'], -.001)
            self.assertLess(abs(check['toe_lowest_z']['right']), .001)
            if check['frame'] <= 25:
                self.assertLess(abs(check['toe_lowest_z']['left']), .001)
            self.assertGreater(check['minimum_local_face_area'], 1e-8)
            a, b = check['bones']['spine']
            self.assertLess(math.degrees(math.acos((b[2]-a[2])/math.dist(a, b))), 10)
        checks = {c['frame']: c for c in self.study['checks']}
        toe = checks[25]['bones']['toe.left']
        self.assertGreater(toe[0][2]-toe[1][2], .05)
        recovery = checks[33]
        self.assertGreater(recovery['toe_lowest_z']['left'], .25)
        self.assertGreater(recovery['bones']['foot.left'][0][2], recovery['bones']['shin.left'][0][2])

    def test_sprite_cells_match_whole_frame_reductions(self):
        for view in ('game', 'side'):
            masters = [Image.open(self.result/f'frames-{view}'/f'pose-{i:02}.png').convert('RGBA') for i in range(1, 34)]
            for size in (96, 128, 512):
                sheet = Image.open(self.result/f'{view}-{size}.png')
                self.assertEqual(sheet.size, (6*size, 6*size))
                for index, master in enumerate(masters):
                    expected = master.resize((size, size), Image.Resampling.LANCZOS)
                    x, y = index%6*size, index//6*size
                    self.assertEqual(sheet.crop((x, y, x+size, y+size)).tobytes(), expected.tobytes())
            with Image.open(self.result/f'{view}-study.gif') as gif:
                self.assertEqual(gif.n_frames, 33)
                for index in range(gif.n_frames):
                    gif.seek(index)
                    gif.load()
        self.assertEqual(len(self.review['frames']), 66)


if __name__ == '__main__':
    unittest.main()
