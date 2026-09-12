"""Package the neutral 3D study without implying a finished run animation."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image, ImageDraw, ImageFilter, ImageFont

BACKGROUND = (29, 36, 32, 255)
POSES = ('neutral', 'reach', 'step')


def flatten(frame, size=384):
    canvas = Image.new('RGBA', (size,size), BACKGROUND)
    canvas.alpha_composite(frame.resize((size,size), Image.Resampling.LANCZOS))
    return canvas.convert('RGB')


def strip(frames):
    width, height = frames[0].size
    result = Image.new('RGBA',(width*len(frames),height))
    for index, frame in enumerate(frames):
        result.paste(frame,(index*width,0))
    return result


def sprite_frames(frames, size):
    small = [frame.resize((size,size),Image.Resampling.LANCZOS) for frame in frames]
    palette = strip(small).convert('RGB').quantize(colors=48)
    results = []
    for frame in small:
        alpha = frame.getchannel('A').point(lambda value: 255 if value >= 128 else 0)
        color = frame.convert('RGB').quantize(palette=palette,dither=Image.Dither.NONE).convert('RGBA')
        color.putalpha(alpha)
        outline = Image.new('RGBA',frame.size,(42,29,20,255))
        outline.putalpha(alpha.filter(ImageFilter.MaxFilter(3)))
        outline.alpha_composite(color)
        results.append(outline)
    return results


def review_html():
    return """<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Storybook mouse · neutral 3D study</title><style>
*{box-sizing:border-box}body{margin:0;background:#17201b;color:#ece7d8;font:16px/1.5 system-ui,sans-serif}main{max-width:1420px;margin:auto;padding:36px}.tag{font-size:12px;color:#cbb77e;letter-spacing:.16em;text-transform:uppercase}h1{font-size:36px;letter-spacing:-.035em;margin:8px 0}p{color:#b2bdb1;max-width:1000px}a{color:#dfc78e}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}.panel{border:1px solid #3a483b;border-radius:12px;overflow:hidden;background:#233026}.panel header{display:flex;justify-content:space-between;align-items:center;padding:14px 18px;border-bottom:1px solid #3a483b}.panel header span{font-size:12px;color:#a9b7aa}.panel img{width:100%;display:block;aspect-ratio:1;object-fit:contain;background:radial-gradient(ellipse at 50% 40%,#3b493b,#202a22)}.controls,.links{display:flex;flex-wrap:wrap;align-items:center;gap:16px;margin:20px 0}button,select{background:#334331;color:#f0ebdc;border:1px solid #637156;padding:9px 12px;border-radius:6px;font:inherit}.lower{display:grid;grid-template-columns:1fr 2fr;gap:16px;margin-top:24px}.note{font-size:14px}.pixel{image-rendering:pixelated}.panel .sheet{aspect-ratio:3}.caption{padding:0 18px 12px;font-size:14px}details{margin:18px 0}summary{cursor:pointer;padding:14px 18px}details img{max-width:768px;margin:auto}
@media(max-width:900px){main{padding:20px}.grid{grid-template-columns:1fr}.lower{grid-template-columns:1fr}.grid .panel{max-width:600px}h1{font-size:29px}}
</style><main><div class="tag">Zebes / Storybook direction / Neutral study 01</div><h1>A new character mesh</h1>
<p>The first 3D head and body in the chosen storybook direction. Compare the modeling reference with the actual clay geometry and the same mesh with simple study colors.</p>
<div class="controls"><label>View <select id="view"><option value="quarter">Three-quarter</option><option value="front">Front</option><option value="side">Side</option><option value="back">Back</option></select></label><label>Pose <select id="pose"><option value="neutral">Neutral</option><option value="reach">Raised arms</option><option value="step">Bent leg</option></select></label></div>
<div class="grid"><section class="panel"><header>Modeling reference<span>2D input</span></header><img src="reference.png" alt="The neutral reference used for shape generation"></section><section class="panel"><header>Clay geometry<span>Actual 3D render</span></header><img id="clay" src="clay-quarter.png" alt="Clay render of the new mouse"></section><section class="panel"><header>Study colors<span>Same 3D mesh</span></header><img id="color" src="color-quarter.png" alt="Textured render of the new mouse"></section></div>
<p class="note">The colors are authored placeholders baked into a texture. This is a neutral shape and body-deformation study: the hands, hair tufts and facial topology still need cleanup. The temporary rig has no facial or finger controls, and the three poses are not a finished run cycle.</p>
<div class="links"><a href="neutral-mouse.blend">Editable Blender study</a><a href="neutral-mouse.glb">Textured GLB with pose checks</a><a href="study-basecolor.png">Study texture</a><a href="sprites-96.png">96px pose sheet</a><a href="sprites-128.png">128px pose sheet</a><a href="review.json">Study metadata</a></div>
<details class="panel"><summary>Inspect the head</summary><div class="grid" style="grid-template-columns:1fr 1fr"><img src="clay-head.png" alt="Clay head detail"><img src="color-head.png" alt="Textured head detail"></div></details>
<div class="lower"><section class="panel"><header>Turntable<span>16 actual views</span></header><img src="turntable.gif" alt="Turntable of the new 3D mouse"></section><section class="panel"><header>Sprite-size checks<select id="size"><option value="96">96px</option><option value="128">128px</option></select></header><img class="sheet pixel" id="sprites" src="sprites-96.png" alt="Neutral, raised-arm and bent-leg sprites"><p class="caption">Neutral · Raised arms · Bent leg. All use one camera, scale and mesh. The sprite images are reduced from the retained renders with one shared palette.</p><img class="sheet" src="pose-grid.png" alt="The three full-size body pose checks"></section></div>
<script>const view=document.getElementById('view'),pose=document.getElementById('pose');function draw(){const p=pose.value;view.disabled=p!=='neutral';for(const mode of ['clay','color'])document.getElementById(mode).src=p==='neutral'?`${mode}-${view.value}.png`:`pose-${p}-${mode}.png`;}view.onchange=draw;pose.onchange=draw;document.getElementById('size').onchange=e=>document.getElementById('sprites').src=`sprites-${e.target.value}.png`;</script></main></html>"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory',type=Path)
    args = parser.parse_args()
    root, out = args.directory, args.directory/'result'
    if (out/'review.json').exists():
        raise ValueError('Review already exists; preserve it before regenerating')
    inspection = json.loads((out/'inspection.json').read_text())
    raw = json.loads((root/'raw-shape/receipt.json').read_text())
    cleanup = json.loads((root/'cleanup/cleanup.json').read_text())
    if raw['status'] != 'complete' or not cleanup['watertight']:
        raise ValueError('The study requires a completed trial and the inspected closed main surface')
    frames = [Image.open(out/f'pose-{pose}-color.png').convert('RGBA') for pose in POSES]
    for pose, frame in zip(POSES,frames):
        bounds=frame.getchannel('A').point(lambda value:255 if value>=128 else 0).getbbox()
        if frame.size != (768,768) or not bounds or min(bounds[:2])<=0 or max(bounds[2:])>=768:
            raise ValueError(f'Clipped or invalid pose render: {pose}')
    for size in (96,128,512):
        images=sprite_frames(frames,size) if size<512 else [f.resize((512,512),Image.Resampling.LANCZOS) for f in frames]
        strip(images).save(out/f'sprites-{size}.png')
    strip([Image.open(out/f'pose-{pose}-color.png').convert('RGBA').resize((384,384)) for pose in POSES]).save(out/'pose-grid.png')
    views=[flatten(Image.open(out/f'turn-{i:02}.png').convert('RGBA')) for i in range(16)]
    palette=views[0].quantize(colors=255)
    views=[im.quantize(palette=palette,dither=Image.Dither.NONE) for im in views]
    views[0].save(out/'turntable.gif',save_all=True,append_images=views[1:],duration=125,loop=0,disposal=2)
    shutil.copyfile(root/'prepared/conditioning-image.png',out/'reference.png')
    comparison=Image.new('RGB',(1152,426),BACKGROUND[:3])
    draw=ImageDraw.Draw(comparison)
    for index,(label,path) in enumerate((('REFERENCE',out/'reference.png'),('CLAY / ACTUAL MESH',out/'clay-quarter.png'),('STUDY COLORS',out/'color-quarter.png'))):
        draw.text((index*384+20,14),label,fill=(226,206,157),font=ImageFont.load_default(size=16))
        comparison.paste(flatten(Image.open(path).convert('RGBA')),(index*384,42))
    comparison.save(out/'comparison.png')
    (out/'review.html').write_text(review_html())
    files=('neutral-mouse.blend','neutral-mouse.glb','study-basecolor.png','sprites-96.png','sprites-128.png','sprites-512.png')
    review={'stage':'Neutral model and two body-pose checks','frame_names':POSES,'sprite_sizes':[96,128,512],
            'raw_faces':raw['faces'],'clean_faces':cleanup['faces'],'study_faces':inspection['study_faces'],
            'shape_inference_seconds':raw['elapsed_seconds'],'color_method':inspection['projection']['method'],
            'limits':['Temporary body binding','No facial or finger controls','Hands, tufts and facial topology need refinement'],
            'sha256':{name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in files}}
    (out/'review.json').write_text(json.dumps(review,indent=2)+'\n')
    shutil.copyfile(__file__,out/Path(__file__).name)
    print(json.dumps(review),flush=True)


if __name__=='__main__':
    main()
