"""Run 02 forefoot mechanics measured from the saved Blender action and exports."""

import hashlib
import json
import math
import unittest

import numpy as np

from experiments.storybook_mouse import run_motion_v2
from tests import storybook_run_test


class StorybookRun02Test(storybook_run_test.StorybookRunTest):
    asset = storybook_run_test.ROOT/'experiments/mouse_3d/storybook-run-v2/result'
    motion = run_motion_v2

    def test_run_01_is_preserved_byte_for_byte(self):
        manifest = json.loads((self.asset.parent/'preservation.json').read_text())['run_01_sha256']
        root = storybook_run_test.ASSET.parent
        self.assertEqual(set(manifest), {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()})
        for name, expected in manifest.items():
            self.assertEqual(hashlib.sha256((root/name).read_bytes()).hexdigest(), expected, name)

    def test_toe_pad_is_planted_for_entire_stance_with_elevated_heel(self):
        self.assertEqual(self.report['contact_marker'], 'pad')
        for side in ('left', 'right'):
            start = None
            for check in self.report['checks']:
                paw = check['paws'][side]
                if not paw['stance']:
                    start = None
                    continue
                pad = np.array(paw['markers']['pad'])
                pad[1] -= self.report['stride']*paw['phase']
                if start is None or paw['phase'] == 0:
                    start = pad
                self.assertLess(float(np.linalg.norm(pad-start)), .001)
                self.assertLess(abs(pad[2]), .001)
                self.assertGreater(paw['markers']['heel'][2], .5)
                # The raised ankle stays behind the knee and the forefoot.
                knee = check['bones'][f'shin.{side}'][0]
                ankle = check['bones'][f'foot.{side}'][0]
                self.assertGreater(ankle[1], knee[1]+.15)
                self.assertGreater(ankle[1], paw['markers']['pad'][1]+.15)
                self.assertGreater(ankle[2], .6)

    def test_heel_compresses_then_extends_into_toe_push_off(self):
        for side in ('left', 'right'):
            stance = sorted((c['paws'][side] for c in self.report['checks']
                             if c['paws'][side]['stance']), key=lambda p: p['phase'])
            heel = lambda p: p['markers']['heel'][2]
            compression = min(stance, key=heel)
            self.assertTrue(.07 < compression['phase'] < .13)
            self.assertGreater(heel(stance[0])-heel(compression), .07)
            self.assertGreater(heel(stance[-1])-heel(compression), .14)

    def test_upright_body_and_faster_cadence_speed_and_recovery(self):
        previous = json.loads((storybook_run_test.ASSET/'motion.json').read_text())
        self.assertEqual(self.report['cycle_seconds'], .5)
        self.assertGreater(self.report['virtual_forward_speed'], previous['virtual_forward_speed']*1.5)
        for fraction in ('stance_end',):
            self.assertLess(self.report[fraction]*self.report['cycle_seconds'],
                            previous[fraction]*previous['cycle_seconds'])
        self.assertLess((1-self.report['stance_end'])*self.report['cycle_seconds'],
                        (1-previous['stance_end'])*previous['cycle_seconds'])
        for check in self.report['checks']:
            a, b = np.array(check['bones']['spine'])
            self.assertLess(math.degrees(math.acos((b-a)[2]/np.linalg.norm(b-a))), 10)
        self.assertEqual(self.review['full_fps'], 48)
        self.assertEqual(self.review['sparse_fps'], 24)
        for frame in self.review['frames']:
            self.assertAlmostEqual(frame['time_seconds'], frame['index']/48)

    def test_recovery_joins_support_with_continuous_velocity(self):
        epsilon = 1e-5
        for boundary in (0, self.motion.STANCE_END):
            a, b, c = [self.motion.paw(boundary+i*epsilon) for i in (-1, 0, 1)]
            for key in ('y', 'clearance', 'pitch'):
                self.assertLess(abs((b[key]-a[key])/epsilon-(c[key]-b[key])/epsilon), .01)


if __name__ == '__main__':
    unittest.main()
