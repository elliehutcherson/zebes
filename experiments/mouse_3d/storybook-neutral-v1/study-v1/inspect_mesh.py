"""Import a retained generated mesh into Blender and render honest shape views.

All inputs are local files. Color projection is a separately labeled appearance
study; clay renders remain the evidence for generated geometry.
"""

import argparse
import json
import math
from pathlib import Path
import shutil
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from study_rig import create_study_rig, pose_study, study_colors


def aim(obj, target):
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z', 'Y').to_euler()


def material(name, color, vertex=False):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Roughness'].default_value = 0.82
    if vertex:
        node = mat.node_tree.nodes.new('ShaderNodeVertexColor')
        node.layer_name = 'ReferenceColor'
        mat.node_tree.links.new(node.outputs['Color'], shader.inputs['Base Color'])
    return mat


def configure():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE'
    scene.eevee.taa_render_samples = 64
    scene.eevee.use_gtao = True
    scene.eevee.gtao_distance = 0.22
    scene.eevee.gtao_factor = 1.05
    scene.eevee.use_soft_shadows = True
    scene.render.resolution_x = scene.render.resolution_y = 768
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.film_transparent = True
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'Medium High Contrast'
    scene.world.color = (0.3, 0.3, 0.3)
    return scene


def load_mesh(path):
    bpy.ops.import_scene.gltf(filepath=str(path))
    objects = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    if not objects:
        raise ValueError('The GLB contains no mesh')
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    if len(objects) > 1:
        bpy.ops.object.join()
    obj = bpy.context.object
    world = obj.matrix_world.copy()
    for vertex in obj.data.vertices:
        vertex.co = world @ vertex.co
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)
    minimum = Vector([min(v.co[k] for v in obj.data.vertices) for k in range(3)])
    maximum = Vector([max(v.co[k] for v in obj.data.vertices) for k in range(3)])
    scale = 4.3/(maximum.z-minimum.z)
    center = Vector(((minimum.x+maximum.x)/2, (minimum.y+maximum.y)/2, minimum.z))
    for vertex in obj.data.vertices:
        vertex.co = (vertex.co-center)*scale
    obj.name = 'Mouse_Neutral'
    original_faces = len(obj.data.polygons)
    if original_faces > 80000:
        modifier = obj.modifiers.new('Neutral study mesh reduction', 'DECIMATE')
        modifier.ratio = 80000/original_faces
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    obj.data.materials.clear()
    obj.data.update()
    return obj, {'imported_faces': original_faces, 'study_faces': len(obj.data.polygons),
                 'study_vertices': len(obj.data.vertices), 'source_bounds': [list(minimum), list(maximum)],
                 'uniform_scale': scale, 'normalization_translation': list(-center),
                 'height': 4.3}


def color_projection(obj, input_dir):
    import numpy as np
    from PIL import Image

    source = np.asarray(Image.open(input_dir/'conditioning-image.png').convert('RGB'), dtype=float)/255
    source = np.where(source <= 0.04045, source/12.92, ((source+0.055)/1.055)**2.4)
    mask = np.asarray(Image.open(input_dir/'conditioning-mask.png').convert('L'))
    ys, xs = np.nonzero(mask > 128)
    bounds = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
    points = [v.co for v in obj.data.vertices]
    x_mid = (min(p.x for p in points)+max(p.x for p in points))/2
    z_min, z_max = min(p.z for p in points), max(p.z for p in points)
    factor = (bounds[3]-bounds[1])/(z_max-z_min)
    cx = (bounds[0]+bounds[2])/2
    base = np.array((0.45, 0.20, 0.055))
    attribute = obj.data.color_attributes.new(name='ReferenceColor', type='FLOAT_COLOR', domain='POINT')
    count = 0
    for vertex, color in zip(obj.data.vertices, attribute.data):
        u = int(round(cx+(vertex.co.x-x_mid)*factor))
        v = int(round(bounds[3]-(vertex.co.z-z_min)*factor))
        weight = max(0, min(1, (-vertex.normal.y-0.03)/0.40))
        rgb = base.copy()
        if 0 <= u < 512 and 0 <= v < 512 and mask[v, u] > 128:
            rgb = base*(1-weight)+source[v, u]*weight
            count += weight > 0
        color.color = (*rgb, 1)
    return {'method': 'Fixed frontal reference projection; reverse/occluded areas use a plain authored fur color',
            'projected_vertices': int(count), 'projection_mask_bounds': bounds,
            'pixels_per_world_unit': factor, 'world_center_x': x_mid}


def render(scene, path, angle, scale=5.1, target=(0, 0, 2.15)):
    scene.camera.location = (12*math.sin(angle), -12*math.cos(angle), target[2]+0.7)
    aim(scene.camera, target)
    scene.camera.data.ortho_scale = scale
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mesh', type=Path, required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--color-mode', choices=('authored','projected'), default='authored')
    parser.add_argument('--with-rig', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if args.out.exists():
        raise ValueError('Keep prior inspection artifacts and choose a new directory')
    args.out.mkdir(parents=True)
    scene = configure()
    obj, summary = load_mesh(args.mesh)
    projection = study_colors(obj) if args.color_mode == 'authored' else color_projection(obj, args.input)
    clay = material('Clay - geometry inspection', (0.51, 0.56, 0.53))
    color = material('Provisional study colors', (0.45, 0.20, 0.055), vertex=True)
    obj.data.materials.append(clay)
    bpy.ops.object.camera_add(location=(0, -12, 2.85))
    scene.camera = bpy.context.object
    scene.camera.name = 'Review_Camera'
    scene.camera.data.type = 'ORTHO'
    for name, location, energy, size, rgb in (
            ('Key', (-3, -5, 7), 500, 4, (1, 0.92, 0.82)),
            ('Fill', (4, -3, 4), 220, 4, (0.79, 0.86, 1)),
            ('Rim', (-2, 4, 5), 500, 3, (1, 0.87, 0.67))):
        bpy.ops.object.light_add(type='AREA', location=location)
        light = bpy.context.object
        light.name = name
        light.data.energy, light.data.size, light.data.color = energy, size, rgb
        light.data.use_contact_shadow = False
        aim(light, (0, 0, 2))
    rig = None
    if args.with_rig:
        rig, summary['binding'] = create_study_rig(obj)
    views = [('front', 0), ('quarter', math.pi/6), ('side', math.pi/2), ('back', math.pi)]
    for mode, mat in (('clay', clay), ('color', color)):
        obj.data.materials[0] = mat
        for name, angle in views:
            render(scene, args.out/f'{mode}-{name}.png', angle)
        render(scene, args.out/f'{mode}-head.png', math.pi/6, 2.6, (0, 0, 3.4))
    obj.data.materials[0] = color
    for i in range(16):
        render(scene, args.out/f'turn-{i:02}.png', i*math.tau/16)
    render(scene, args.out/'head-detail.png', math.pi/6, 2.6, (0, 0, 3.4))
    if rig:
        scene.render.fps = 12
        scene.frame_start, scene.frame_end = 1, 25
        summary['pose_checks'] = []
        for frame, name in ((1,'neutral'), (13,'reach'), (25,'step')):
            scene.frame_set(frame)
            pose_study(rig, name)
            scene.timeline_markers.new(name.title(), frame=frame)
            for bone in rig.pose.bones:
                bone.keyframe_insert(data_path='rotation_euler', frame=frame, group=bone.name)
            evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            surface = evaluated.to_mesh()
            lowest = min((evaluated.matrix_world @ vertex.co).z for vertex in surface.vertices)
            evaluated.to_mesh_clear()
            summary['pose_checks'].append({'name': name, 'frame': frame, 'lowest_z': lowest,
                                          'bones': {bone.name: [list(bone.head),list(bone.tail)] for bone in rig.pose.bones}})
            for mode, mat in (('clay',clay),('color',color)):
                obj.data.materials[0] = mat
                render(scene, args.out/f'pose-{name}-{mode}.png', math.pi/6)
        rig.animation_data.action.name = 'Neutral_and_two_body_checks'
        for curve in rig.animation_data.action.fcurves:
            for key in curve.keyframe_points:
                key.interpolation = 'LINEAR'
        scene.frame_set(1)
        pose_study(rig, 'neutral')
    obj.data.materials[0] = color
    render(scene, args.out/'hero.png', math.pi/6)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if rig:
        rig.select_set(True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'neutral-mouse.blend'))
    bpy.ops.export_scene.gltf(filepath=str(args.out/'neutral-mouse.glb'), use_selection=True, export_format='GLB',
                              export_anim_slide_to_zero=True)
    summary.update(projection=projection, source_mesh=str(args.mesh),
                   status='Neutral geometry study with provisional colors; temporary body rig' if rig else
                          'Neutral geometry study; provisional colors; no rig',
                   color_mode=args.color_mode)
    (args.out/'inspection.json').write_text(json.dumps(summary, indent=2)+'\n')
    for source in (Path(__file__), Path(__file__).with_name('study_rig.py')):
        shutil.copyfile(source, args.out/source.name)
    print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    main()
