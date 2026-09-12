"""Retained neutral-study artifact checks, independent of Blender and CUDA."""

import hashlib
import json
import math
from pathlib import Path
import struct
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]/'experiments/mouse_3d/storybook-neutral-v1'
RESULT = ROOT/'result'


def glb_document():
    data = (RESULT/'neutral-mouse.glb').read_bytes()
    magic, version, size = struct.unpack_from('<4sII',data)
    if (magic,version,size) != (b'glTF',2,len(data)):
        raise ValueError('Invalid GLB container')
    json_size, kind = struct.unpack_from('<I4s',data,12)
    if kind != b'JSON':
        raise ValueError('GLB metadata chunk missing')
    document=json.loads(data[20:20+json_size])
    binary_size, binary_kind=struct.unpack_from('<I4s',data,20+json_size)
    if binary_kind != b'BIN\x00':
        raise ValueError('GLB data chunk missing')
    return document,data[28+json_size:28+json_size+binary_size]


def accessor(document,binary,index):
    access=document['accessors'][index]
    view=document['bufferViews'][access['bufferView']]
    dtype=np.dtype({5126:'<f4',5123:'<u2',5121:'u1',5125:'<u4'}[access['componentType']])
    components={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[access['type']]
    offset=view.get('byteOffset',0)+access.get('byteOffset',0)
    stride=view.get('byteStride',dtype.itemsize*components)
    values=np.ndarray((access['count'],components),dtype=dtype,buffer=binary,offset=offset,strides=(stride,dtype.itemsize))
    if access.get('normalized',False):
        values=values.astype(float)/np.iinfo(dtype).max
    return values


class StorybookNeutralAssetTest(unittest.TestCase):
    def test_raw_trial_and_inputs_remain_unchanged(self):
        receipt=json.loads((ROOT/'raw-shape/receipt.json').read_text())
        inputs=json.loads((ROOT/'prepared/inputs.json').read_text())
        self.assertEqual(receipt['status'],'complete')
        self.assertEqual(receipt['input'],inputs)
        for name,expected in receipt['raw_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT/'raw-shape'/name).read_bytes()).hexdigest(),expected)

    def test_cleanup_is_closed_and_preserves_the_character_bounds(self):
        raw=json.loads((ROOT/'raw-shape/receipt.json').read_text())
        clean=json.loads((ROOT/'cleanup/cleanup.json').read_text())
        self.assertTrue(clean['watertight'])
        self.assertLess(clean['removed_fragment_faces']/clean['raw_faces'],0.005)
        np.testing.assert_allclose(clean['bounds'],raw['bounds'],atol=1e-6)

    def test_texture_contains_color_and_exports_inside_the_glb(self):
        with Image.open(RESULT/'study-basecolor.png') as image:
            self.assertEqual(image.size,(2048,2048))
            pixels=np.asarray(image.convert('RGB'))
            self.assertGreater(int(pixels.max()),100)
            self.assertGreater(np.count_nonzero(pixels.max(axis=2)>30),100000)
        document,_=glb_document()
        self.assertTrue(document['images'])
        for image in document['images']:
            self.assertIn('bufferView',image)
        for material in document['materials']:
            self.assertIn('baseColorTexture',material['pbrMetallicRoughness'])

    def test_exported_mesh_is_skinned_with_valid_uvs_and_normalized_weights(self):
        document,binary=glb_document()
        self.assertTrue(document['skins'])
        self.assertTrue(document['animations'])
        joint_count=len(document['skins'][0]['joints'])
        for mesh in document['meshes']:
            for primitive in mesh['primitives']:
                attributes=primitive['attributes']
                points=accessor(document,binary,attributes['POSITION'])
                weights=accessor(document,binary,attributes['WEIGHTS_0'])
                joints=accessor(document,binary,attributes['JOINTS_0'])
                uv=accessor(document,binary,attributes['TEXCOORD_0'])
                self.assertTrue(np.isfinite(points).all())
                self.assertTrue(np.isfinite(uv).all())
                self.assertEqual(len(weights),len(points))
                self.assertGreaterEqual(float(weights.min()),0)
                np.testing.assert_allclose(weights.sum(axis=1),1,atol=1e-5)
                self.assertLess(int(joints.max()),joint_count)

    def test_pose_checks_keep_bone_lengths_attachments_and_ground(self):
        report=json.loads((RESULT/'inspection.json').read_text())
        self.assertEqual(report['binding']['unweighted_vertices'],0)
        checks=report['pose_checks']
        self.assertEqual([p['name'] for p in checks],['neutral','reach','step'])
        neutral=checks[0]['bones']
        for pose in checks:
            self.assertGreaterEqual(pose['lowest_z'],-1e-6)
            for name,points in pose['bones'].items():
                self.assertAlmostEqual(math.dist(*points),math.dist(*neutral[name]),places=5)
            for side in ('left','right'):
                for child,parent in (('forearm','upper_arm'),('hand','forearm'),('shin','thigh'),('foot','shin')):
                    self.assertLess(math.dist(pose['bones'][f'{child}.{side}'][0],pose['bones'][f'{parent}.{side}'][1]),1e-5)
            np.testing.assert_allclose(pose['bones']['head'],neutral['head'],atol=1e-6)
            np.testing.assert_allclose(pose['bones']['foot.left'],neutral['foot.left'],atol=1e-6)

    def test_renders_and_sprite_cells_have_transparent_margins(self):
        for pose in ('neutral','reach','step'):
            for mode in ('clay','color'):
                with Image.open(RESULT/f'pose-{pose}-{mode}.png') as image:
                    self.assertEqual(image.size,(768,768))
                    bounds=image.getchannel('A').point(lambda v:255 if v>=128 else 0).getbbox()
                    self.assertIsNotNone(bounds)
                    self.assertGreater(min(bounds[:2]),0)
                    self.assertLess(max(bounds[2:]),768)
        for size in (96,128,512):
            with Image.open(RESULT/f'sprites-{size}.png') as image:
                self.assertEqual(image.size,(size*3,size))
                for index in range(3):
                    cell=image.crop((index*size,0,(index+1)*size,size))
                    bounds=cell.getchannel('A').getbbox()
                    self.assertGreater(min(bounds[:2]),0)
                    self.assertLess(max(bounds[2:]),size)


if __name__=='__main__':
    unittest.main()
