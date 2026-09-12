"""Run 03 motion boundaries, preserved inputs and saved Blender/export evidence."""

import hashlib
import json
import math
from pathlib import Path
import unittest

from PIL import Image

from experiments.storybook_mouse import run_motion_v3 as motion

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT/'experiments/mouse_3d/storybook-run-v3'


class Run03PathsTest(unittest.TestCase):
    def test_reference_cadence_and_longer_stride(self):
        self.assertAlmostEqual(motion.CYCLE_SECONDS, 16/30)
        self.assertGreater(motion.STRIDE, 2.6)
        self.assertAlmostEqual(motion.SPEED*motion.CYCLE_SECONDS, motion.STRIDE)
        self.assertLess(motion.STANCE_END, .5)

    def test_support_and_recovery_join_without_position_or_velocity_jump(self):
        epsilon = 1e-6
        for phase in (0, motion.STANCE_END):
            for key in ('y','clearance','foot_pitch','toe_pitch'):
                a, b, c = (motion.paw(p)[key] for p in (phase-epsilon,phase,phase+epsilon))
                self.assertLess(abs(a-c), 2e-5, (phase,key))
                self.assertLess(abs((b-a)/epsilon-(c-b)/epsilon), .002, (phase,key))
        for i in range(1001):
            p = i/1000
            path = motion.paw(p)
            self.assertGreaterEqual(path['clearance'], -1e-8)
            if path['stance']:
                self.assertAlmostEqual(path['clearance'], 0)
                self.assertAlmostEqual(path['y']-motion.STRIDE*(p%1), motion.paw(0)['y'])

    def test_arms_oppose_and_elbows_and_wrists_change(self):
        for i in range(100):
            p=i/100
            self.assertAlmostEqual(motion.arm(p)['swing'], -motion.arm(p+.5)['swing'])
        values=[motion.arm(i/100) for i in range(100)]
        self.assertGreater(max(p['bend'] for p in values)-min(p['bend'] for p in values), .6)
        self.assertGreater(max(p['wrist'] for p in values)-min(p['wrist'] for p in values), .25)


class Run03ArtifactsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result=BUNDLE/'result'
        cls.motion=json.loads((cls.result/'motion.json').read_text())
        cls.review=json.loads((cls.result/'review.json').read_text())

    def test_all_four_prior_bundles_are_preserved(self):
        manifest=json.loads((BUNDLE/'preservation.json').read_text())
        self.assertEqual(set(manifest), {'storybook-neutral-v1','storybook-run-v1',
                                         'storybook-run-v2','storybook-foot-study-v1'})
        for name,files in manifest.items():
            directory=BUNDLE.parent/name
            self.assertEqual(set(files),{str(p.relative_to(directory)) for p in directory.rglob('*') if p.is_file()})
            for path,expected in files.items():
                self.assertEqual(hashlib.sha256((directory/path).read_bytes()).hexdigest(),expected,f'{name}/{path}')

    def test_local_weight_repair_preserves_other_source_data(self):
        edits=json.loads((self.result/'weight-edits.json').read_text())
        self.assertTrue(edits['geometry_rest_rig_uv_texture_unchanged'])
        self.assertEqual(*edits['outside_region_weight_sha256_before_after'])
        self.assertTrue(edits['changed_weight_indices'])
        self.assertLessEqual(set(edits['changed_weight_indices']),set(edits['region_vertex_indices']))
        self.assertGreater(edits['region_bounds'][0][2], .24)
        self.assertLess(edits['region_bounds'][1][2], .84)
        for row in edits['weights']:
            for key in set(row['before'])|set(row['after']):
                if not key.startswith(('shin.','foot.')):
                    self.assertEqual(row['before'].get(key,0),row['after'].get(key,0))
            self.assertAlmostEqual(sum(row['after'].values()),1,delta=1e-6)

    def test_fist_shape_is_local_reversible_and_saved(self):
        fists=json.loads((self.result/'fist-edits.json').read_text())
        self.assertEqual(fists['name'],'Running_fists')
        self.assertGreater(len(fists['changed_vertices']),100)
        self.assertTrue(self.motion['saved_copy_verified'])
        self.assertEqual(set(self.motion['saved_shape_sha256']),{'Basis','Running_fists'})
        for row in fists['changed_vertices']:
            self.assertGreater(row['hand_weight'],.02)
            self.assertTrue(all(math.isfinite(v) for v in row['after']))
        for check in self.motion['checks']:
            self.assertEqual(check['fist_value'],1)

    def test_saved_action_lengths_attachments_posture_and_loop(self):
        self.assertEqual(len(self.motion['checks']),385)
        self.assertEqual(len(self.motion['rest_bones']),22)
        for c in self.motion['checks']:
            for name,points in c['bones'].items():
                self.assertAlmostEqual(math.dist(*points),self.motion['rest_bones'][name]['length'],delta=1e-5)
            for side in ('left','right'):
                for parent,child in (('thigh','shin'),('shin','foot'),('foot','toe'),
                                     ('upper_arm','forearm'),('forearm','hand')):
                    self.assertLess(math.dist(c['bones'][f'{parent}.{side}'][1],c['bones'][f'{child}.{side}'][0]),1e-5)
            a,b=c['bones']['spine']
            lean=math.degrees(math.acos((b[2]-a[2])/math.dist(a,b)))
            self.assertGreater(lean,10)
            self.assertLess(lean,16)
        first,last=self.motion['checks'][0],self.motion['checks'][-1]
        for name in first['bones']:
            for a,b in zip(first['bones'][name],last['bones'][name]):
                self.assertLess(math.dist(a,b),1e-6)

    def test_ground_contact_and_flat_patch_are_stable(self):
        previous={}
        for c in self.motion['checks']:
            self.assertGreater(c['lowest_z'],-.001)
            for side,paw in c['paws'].items():
                if paw['stance']:
                    self.assertLess(abs(paw['lowest_z']),.001)
                    self.assertAlmostEqual(paw['markers']['pad'][1],paw['y'],delta=.001)
                patch=paw.get('patch_ground_space')
                if patch is None:
                    previous.pop(side,None)
                    continue
                self.assertGreater(len(patch),10)
                if side in previous and c['phase'] != 1:
                    self.assertLess(max(math.dist(a,b) for a,b in zip(patch,previous[side])),.001)
                previous[side]=patch

    def test_forearms_and_hands_clear_the_body(self):
        for c in self.motion['checks']:
            for side,clearance in c['arm_clearance'].items():
                self.assertEqual(clearance['forearm_hand_body_triangle_overlaps'],0,(c['phase'],side))
                self.assertGreater(clearance['min_hand_body_distance'],.005,(c['phase'],side))
            self.assertGreater(c['minimum_face_area'],1e-9)

    def test_hock_repair_reduces_stretching_in_identical_poses(self):
        comparison=json.loads((self.result/'deformation-comparison.json').read_text())
        self.assertTrue(comparison['same_leg_poses'])
        self.assertLess(comparison['after_max_hock_edge_ratio'],comparison['before_max_hock_edge_ratio'])

    def test_frames_sheets_and_animation_decode_at_declared_sizes_and_timing(self):
        self.assertEqual(len(self.review['frames']),48)
        for view in ('game','side'):
            masters=[Image.open(self.result/f'frames-{view}'/f'run-{i:02}.png').convert('RGBA') for i in range(24)]
            for size in (96,128,512):
                expected=[f.resize((size,size),Image.Resampling.LANCZOS) for f in masters]
                for step in (1,2):
                    suffix='' if step==1 else '-12'
                    with Image.open(self.result/f'{view}-{size}{suffix}.png') as sheet:
                        for i,frame in enumerate(expected[::step]):
                            x,y=i%6*size,i//6*size
                            self.assertEqual(sheet.crop((x,y,x+size,y+size)).tobytes(),frame.tobytes())
                    if size==512:
                        continue
                    with Image.open(self.result/f'{view}-{size}-{24//step}.apng') as apng:
                        self.assertEqual(apng.n_frames,24//step)
                        total=0
                        for i,frame in enumerate(expected[::step]):
                            apng.seek(i)
                            total+=apng.info['duration']
                            self.assertEqual(apng.convert('RGBA').tobytes(),frame.tobytes())
                        self.assertAlmostEqual(total,1000*motion.CYCLE_SECONDS,delta=1)
        for meta in self.review['frames']:
            self.assertGreater(min(meta['bounds_512'][:2]),0)
            self.assertLess(max(meta['bounds_512'][2:]),512)
            self.assertEqual(meta['origin_512'],[256,self.motion['camera']['ground_row_512']])

    def test_artifacts_match_manifest_and_reference_is_attributed(self):
        hashes=json.loads((self.result/'artifact-sha256.json').read_text())
        for path,expected in hashes.items():
            self.assertEqual(hashlib.sha256((self.result/path).read_bytes()).hexdigest(),expected,path)
        reference=self.review['reference']
        self.assertEqual(reference['creator'],'pistachio')
        self.assertEqual(reference['license'],'CC BY 4.0')
        self.assertAlmostEqual(reference['cycle_seconds'],self.motion['cycle_seconds'])


if __name__=='__main__':
    unittest.main()
