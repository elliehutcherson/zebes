"""Exercise live rig code against saved studies; run with Blender's Python.

blender --background --threads 4 --python-exit-code 1 \
  --python tests/tools/storybook_rig_test.py
"""

import json
import math
from pathlib import Path
import sys
import unittest

import bpy
from mathutils import Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'experiments/storybook_mouse'))

import animate_run
import animate_run_v3
import foot_study
import rig_support
import run_motion
import run_motion_v2


ASSETS = ROOT / 'experiments/mouse_3d'


class StorybookRigTest(unittest.TestCase):
    def load(self, bundle, filename):
        bpy.ops.wm.open_mainfile(filepath=str(ASSETS / bundle / 'result' / filename))
        obj = bpy.data.objects['Mouse_Neutral']
        rig = bpy.data.objects['Mouse_Study_Rig']
        rig.animation_data.action = None
        for bone in rig.pose.bones:
            bone.rotation_mode = 'QUATERNION'
        rig_support.reset(rig)
        return obj, rig

    def report(self, bundle, filename='motion.json'):
        return json.loads((ASSETS / bundle / 'result' / filename).read_text())

    def assert_pose(self, rig, expected):
        for name, points in expected['bones'].items():
            actual = rig.pose.bones[name]
            for endpoint, value in zip((actual.head, actual.tail), points):
                self.assertLess(math.dist(endpoint, value), 2e-5,
                                (expected['frame'], name, list(endpoint), value))

    def test_runs_01_and_02_reproduce_saved_key_poses_and_preserve_source(self):
        for revision, motion in ((1, run_motion), (2, run_motion_v2)):
            with self.subTest(revision=revision):
                obj, rig = self.load('storybook-neutral-v1', 'neutral-mouse.blend')
                report = self.report(f'storybook-run-v{revision}')
                before = rig_support.fingerprint(obj, rig)
                self.assertEqual(before, report['before'])
                geometry = rig_support.paw_geometry(obj, rig, getattr(motion, 'SUPPORT_PITCH', None))
                for check in report['checks']:
                    if float(check['frame']).is_integer():
                        animate_run.pose(rig, geometry, check['phase'], motion)
                        self.assert_pose(rig, check)
                self.assertEqual(rig_support.fingerprint(obj, rig), before)

    def test_foot_study_reproduces_all_five_saved_poses(self):
        obj, rig = self.load('storybook-foot-study-v1', 'foot-study.blend')
        before = rig_support.fingerprint(obj, rig)
        geometry = foot_study.foot_geometry(obj, rig)
        report = self.report('storybook-foot-study-v1', 'study.json')
        for check in report['checks']:
            if check['frame'] in foot_study.POSES:
                foot_study.pose(obj, rig, geometry, check['frame'])
                self.assert_pose(rig, check)
        self.assertEqual(rig_support.fingerprint(obj, rig), before)

    def test_run_03_reproduces_saved_key_poses(self):
        obj, rig = self.load('storybook-foot-study-v1', 'foot-study.blend')
        geometry = foot_study.foot_geometry(obj, rig)
        for geo in geometry.values():
            geo['pivot'] = min(geo['toes'], key=lambda i: (
                Quaternion((1, 0, 0), .42) @ obj.data.vertices[i].co).z)
            geo['tip'] = min(geo['toes'], key=lambda i: obj.data.vertices[i].co.y)
        for check in self.report('storybook-run-v3')['checks']:
            if float(check['frame']).is_integer():
                animate_run_v3.pose(obj, rig, geometry, check['phase'])
                self.assert_pose(rig, check)

    def test_two_bone_solver_preserves_lengths_and_rejects_unreachable_targets(self):
        start, end, pole = Vector((0, 0, 0)), Vector((0, 0, 1)), Vector((0, -1, 0))
        knee = rig_support.two_bone_joint(start, end, .8, .7, pole)
        self.assertAlmostEqual((knee-start).length, .8, places=6)
        self.assertAlmostEqual((end-knee).length, .7, places=6)
        for target in (start, Vector((0, 0, 4))):
            with self.assertRaisesRegex(ValueError, 'Unreachable paw'):
                rig_support.two_bone_joint(start, target, .8, .7, pole)


if __name__ == '__main__':
    unittest.main(argv=[__file__])
