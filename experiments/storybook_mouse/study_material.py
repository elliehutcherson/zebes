"""Bake authored 3D color fields into a portable texture for the neutral study."""

import math
import bpy
import numpy as np


def bake_study_material(obj, output):
    mat = bpy.data.materials.new('Authored study texture')
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    geometry = nodes.new('ShaderNodeNewGeometry')
    split = nodes.new('ShaderNodeSeparateXYZ')
    links.new(geometry.outputs['Position'], split.inputs[0])
    x, y, z = split.outputs

    def assign(socket, value):
        if isinstance(value, bpy.types.NodeSocket):
            links.new(value, socket)
        else:
            socket.default_value = value

    def calculate(operation, *values):
        node = nodes.new('ShaderNodeMath')
        node.operation = operation
        for socket, value in zip(node.inputs, values):
            assign(socket, value)
        return node.outputs[0]

    def smooth(a, b, value):
        node = nodes.new('ShaderNodeMapRange')
        node.interpolation_type = 'SMOOTHSTEP'
        node.clamp = True
        assign(node.inputs['Value'], value)
        node.inputs['From Min'].default_value = a
        node.inputs['From Max'].default_value = b
        return node.outputs[0]

    def one_minus(value):
        return calculate('SUBTRACT', 1, value)

    def multiply(*values):
        value = values[0]
        for other in values[1:]:
            value = calculate('MULTIPLY', value, other)
        return value

    def ellipse(cx, cz, rx, rz):
        u = calculate('DIVIDE', calculate('SUBTRACT', x, cx), rx)
        v = calculate('DIVIDE', calculate('SUBTRACT', z, cz), rz)
        return calculate('SQRT', calculate('ADD', multiply(u,u), multiply(v,v)))

    def mix(a, b, factor):
        node = nodes.new('ShaderNodeMixRGB')
        assign(node.inputs[0], factor)
        assign(node.inputs[1], a)
        assign(node.inputs[2], b)
        return node.outputs[0]

    base, cream = (0.44,0.205,0.064,1), (0.82,0.64,0.39,1)
    front = one_minus(smooth(-0.85,-0.48,y))
    rgb = mix(base, cream, multiply(one_minus(smooth(0.72,1.03,ellipse(0.17,1.94,0.31,0.66))),front))
    head = calculate('GREATER_THAN', z, 2.68)
    cheek = multiply(one_minus(smooth(3.02,3.18,z)), smooth(2.68,2.85,z),
                     one_minus(smooth(0.48,0.70,calculate('ABSOLUTE',calculate('SUBTRACT',x,0.18)))), front, head)
    rgb = mix(rgb,cream,cheek)
    for cx, cz in ((-0.10,3.215),(0.475,3.225)):
        patch = multiply(one_minus(smooth(0.86,1.05,ellipse(cx,cz,0.185,0.255))),front,head)
        eye = mix((0.94,0.875,0.70,1),(0.19,0.077,0.022,1),
                  one_minus(smooth(0.89,1.01,ellipse(cx+0.025,cz+0.025,0.113,0.172))))
        eye = mix(eye,(0.008,0.005,0.003,1),one_minus(smooth(0.88,1.03,ellipse(cx+0.030,cz+0.031,0.070,0.135))))
        eye = mix(eye,(1,0.97,0.86,1),one_minus(smooth(0.65,1.05,ellipse(cx-0.012,cz+0.099,0.026,0.036))))
        rgb = mix(rgb,eye,patch)
        dx = calculate('SUBTRACT',x,cx)
        brow_z = calculate('SUBTRACT',cz+0.31,multiply(1.1,dx,dx))
        normalized = calculate('DIVIDE',calculate('SUBTRACT',z,brow_z),0.026)
        brow = multiply(calculate('EXPONENT',multiply(-1,normalized,normalized)),
                        one_minus(smooth(0.14,0.20,calculate('ABSOLUTE',dx))),front,head)
        rgb = mix(rgb,(0.19,0.080,0.022,1),brow)
    nose = multiply(one_minus(smooth(0.77,1.05,ellipse(0.18,2.995,0.135,0.092))),
                    one_minus(smooth(-1.10,-1.04,y)),head)
    rgb = mix(rgb,(0.43,0.115,0.060,1),nose)
    for cx, cz in ((-0.78,3.82),(1.01,3.85)):
        bowl = multiply(one_minus(smooth(0.72,1.02,ellipse(cx,cz,0.43,0.47))),
                        one_minus(smooth(-0.68,-0.40,y)),calculate('GREATER_THAN',z,3.32))
        rgb = mix(rgb,(0.68,0.285,0.185,1),bowl)
    rgb = mix(rgb,(0.66,0.40,0.18,1),multiply(one_minus(smooth(0.18,0.34,z)),front))
    tuft = multiply(smooth(0.85,1.25,multiply(-1,x)),smooth(0.90,1.40,z),calculate('GREATER_THAN',y,0.20))
    rgb = mix(rgb,cream,tuft)

    emission = nodes.new('ShaderNodeEmission')
    links.new(rgb,emission.inputs[0])
    output_node = nodes.new('ShaderNodeOutputMaterial')
    links.new(emission.outputs[0],output_node.inputs['Surface'])
    texture = bpy.data.images.new('Study_BaseColor',width=2048,height=2048,alpha=False)
    texture_node = nodes.new('ShaderNodeTexImage')
    texture_node.image = texture
    nodes.active = texture_node
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=0.008)
    bpy.ops.object.mode_set(mode='OBJECT')
    scene = bpy.context.scene
    previous_engine = scene.render.engine
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 1
    scene.cycles.use_denoising = False
    scene.render.bake.margin = 8
    status = bpy.ops.object.bake(type='EMIT')
    pixels = np.empty(2048*2048*4,dtype=np.float32)
    texture.pixels.foreach_get(pixels)
    if status != {'FINISHED'} or pixels.reshape(-1,4)[:,:3].max() < 0.05:
        raise RuntimeError('Color bake did not produce usable image pixels')
    texture.filepath_raw = str(output/'study-basecolor.png')
    texture.file_format = 'PNG'
    texture.save()
    texture.pack()
    shader = nodes.new('ShaderNodeBsdfPrincipled')
    shader.inputs['Roughness'].default_value = 0.82
    links.new(texture_node.outputs['Color'],shader.inputs['Base Color'])
    links.new(shader.outputs[0],output_node.inputs['Surface'])
    scene.render.engine = previous_engine
    for attribute in tuple(obj.data.color_attributes):
        obj.data.color_attributes.remove(attribute)
    return mat, {'method': 'Authored 3D color fields baked to UV texture', 'texture_size': [2048,2048],
                 'texture': 'study-basecolor.png', 'facial_controls': 'not implemented'}
