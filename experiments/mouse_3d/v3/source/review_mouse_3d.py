"""Package retained Blender frames as transparent sheets and a local review."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "experiments/character_binding/inputs/interactive-run-source-v1.png"
BACKGROUND = (28, 34, 33, 255)


def pack(frames, columns=6):
    width, height = frames[0].size
    if len(frames) % columns or any(frame.size != (width, height) for frame in frames):
        raise ValueError("sheet needs equally sized frames and complete rows")
    sheet = Image.new("RGBA", (width * columns, height * (len(frames) // columns)))
    for index, frame in enumerate(frames):
        sheet.paste(frame, (index % columns * width, index // columns * height))
    return sheet


def pixel_frames(frames, size):
    small = [frame.resize((size, size), Image.Resampling.LANCZOS) for frame in frames]
    palette = pack(small).convert("RGB").quantize(colors=48, method=Image.Quantize.MEDIANCUT)
    results = []
    for frame in small:
        alpha = frame.getchannel("A").point(lambda value: 255 if value >= 128 else 0)
        colored = frame.convert("RGB").quantize(palette=palette, dither=Image.Dither.NONE).convert("RGBA")
        colored.putalpha(alpha)
        outline = Image.new("RGBA", frame.size, (42, 28, 21, 255))
        outline.putalpha(alpha.filter(ImageFilter.MaxFilter(3)))
        outline.alpha_composite(colored)
        results.append(outline)
    return results


def gif(frames, path, duration, size=384, pixel=False):
    flattened = []
    for frame in frames:
        canvas = Image.new("RGBA", (size, size), BACKGROUND)
        canvas.alpha_composite(frame.resize((size, size), Image.Resampling.NEAREST if pixel else Image.Resampling.LANCZOS))
        flattened.append(canvas.convert("RGB"))
    palette = flattened[0].quantize(colors=255)
    encoded = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in flattened]
    encoded[0].save(path, save_all=True, append_images=encoded[1:], duration=duration, loop=0, disposal=2, optimize=False)


def html(has_previous=False):
    result = """<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mouse · 3D to sprite study</title><style>
*{box-sizing:border-box}body{margin:0;background:#141b19;color:#eee9dc;font:16px/1.5 system-ui,sans-serif}main{max-width:1440px;margin:auto;padding:38px}
.eyebrow{color:#c9b675;text-transform:uppercase;letter-spacing:.18em;font-size:12px}h1{font-size:38px;font-weight:600;letter-spacing:-.04em;margin:8px 0}p{color:#aebbb2;max-width:940px}a{color:#dfc78e}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:16px}.panel{background:#202a25;border:1px solid #364139;border-radius:12px;overflow:hidden}.panel header{padding:14px 18px;border-bottom:1px solid #364139;display:flex;justify-content:space-between;align-items:center}.panel header span{color:#a5b3a7;font-size:12px}
canvas,.art{display:block;width:100%;height:auto;aspect-ratio:1;object-fit:contain}.art{image-rendering:pixelated}.stage{background:radial-gradient(ellipse at 50% 38%,#354036,#1b2420 75%)}.controls{display:flex;flex-wrap:wrap;gap:16px;align-items:center;padding:20px 0}button,select{background:#303e33;border:1px solid #53624f;color:#eee9dc;border-radius:6px;padding:9px 12px;font:inherit}input{accent-color:#c9b675}input[type=range]{flex:1;min-width:200px}.note{font-size:14px}.lower{display:grid;grid-template-columns:1fr 2fr;gap:16px;margin-top:22px}.lower img{max-width:100%;display:block}.links{display:flex;flex-wrap:wrap;gap:18px;margin:20px 0}.metric{color:#cfdbca;font-size:14px}.sheet{image-rendering:pixelated;background:#253027}
@media(max-width:900px){main{padding:20px}.grid{grid-template-columns:1fr 1fr}.grid .panel:first-child{grid-column:1/-1;max-width:280px}.lower{grid-template-columns:1fr}h1{font-size:30px}}
</style><main><div class="eyebrow">Zebes / Character experiment / 03</div><h1>Mouse refinement: face and clothing</h1>
<p>A stouter silhouette, a shorter muzzle, rounder eyes with warm irises, and folds shaped into the trousers. Compare each frame with the hood-down version you preferred, or switch to the original drawing.</p>
<div class="grid"><section class="panel"><header>Compare with<select id="reference-choice" aria-label="Comparison reference">__PREVIOUS_OPTION__<option value="source">Original drawing</option></select></header><div class="stage"><canvas id="reference" width="512" height="512"></canvas></div></section>
<section class="panel"><header>Model render<span>512 × 512</span></header><div class="stage"><canvas id="render" width="512" height="512"></canvas></div></section>
<section class="panel"><header>Sprite export<select id="resolution" aria-label="Sprite resolution"><option value="96">96 px</option><option value="128">128 px</option></select></header><div class="stage"><canvas id="pixel" width="512" height="512"></canvas></div></section></div>
<div class="controls"><button id="play">Pause</button><input id="frame" aria-label="Animation frame" type="range" min="0" max="23" value="0"><span id="counter">01 / 24</span><select id="cadence" aria-label="Animation sampling"><option value="24">24 frames · 24 fps</option><option value="12">12 frames · 12 fps</option></select><label><input type="checkbox" id="rig"> Show limb joints</label></div>
<div class="metric">One armature · Fixed bone lengths · Grounded support soles · Shared 48-color sprite palette</div>
<p class="note">The small sprites are reduced from the same 512 px renders with a fixed palette and a one-pixel outline. The run is newly authored for this 3D rig; it does not claim to reproduce the previous twelve-pose drawing. The turntable below shows actual views of the mesh.</p>
<div class="links"><a href="mouse.blend">Editable Blender asset</a><a href="mouse.glb">Portable animated GLB</a><a href="sheet-96.png">96 px sprite sheet</a><a href="sheet-128.png">128 px sprite sheet</a><a href="sheet-512.png">512 px sprite sheet</a><a href="sprites.json">Frame metadata</a></div>
<details class="panel"><summary style="padding:14px 18px;cursor:pointer">Inspect the rebuilt face</summary><img src="face-detail.png" alt="Close view of the continuous muzzle, inset eyes and cupped ears" style="display:block;width:min(100%,768px);margin:auto"></details>
<details class="panel" style="margin-top:12px"><summary style="padding:14px 18px;cursor:pointer">Inspect the trouser folds</summary><img src="pants-detail.png" alt="Shaped fabric folds around the knees and boot cuffs" style="display:block;width:min(100%,768px);margin:auto"></details>
<div class="lower"><section class="panel"><header>Turntable<span>16 rendered views</span></header><img src="turntable.gif" alt="Turntable of the 3D mouse"></section><section class="panel"><header>All 24 poses<span>96 px frames · 6 columns</span></header><img class="sheet" src="sheet-96.png" alt="Complete mouse run sprite sheet"><p style="padding:0 18px">The Blender file preserves the materials, separate mesh parts, vertex weights and editable animation. GLB carries the rig and base colors; Blender's procedural cloth detail is not baked into that portable preview.</p></section></div>
<script>
const sheets={},size=512;let f=0,playing=true,last=0,metadata=null;
for(const n of [512,128,96]){sheets[n]=new Image();sheets[n].src=`sheet-${n}.png`;sheets[n].onload=draw;}
const source=new Image();source.src='source.png';source.onload=draw;__PREVIOUS_SETUP__
fetch('sprites.json').then(r=>r.json()).then(m=>{metadata=m;draw()});
const render=document.getElementById('render'),pixel=document.getElementById('pixel'),slider=document.getElementById('frame'),play=document.getElementById('play');
function paint(canvas,n){const c=canvas.getContext('2d');c.clearRect(0,0,size,size);c.imageSmoothingEnabled=n===512;if(sheets[n].complete)c.drawImage(sheets[n],(f%6)*n,Math.floor(f/6)*n,n,n,0,0,size,size);}
function draw(){paint(render,512);paint(pixel,Number(document.getElementById('resolution').value));const reference=document.getElementById('reference').getContext('2d');reference.clearRect(0,0,512,512);reference.imageSmoothingEnabled=false;if(document.getElementById('reference-choice').value==='previous'){if(previous.complete)reference.drawImage(previous,(f%6)*512,Math.floor(f/6)*512,512,512,0,0,512,512)}else if(source.complete)reference.drawImage(source,0,0,512,512);slider.value=f;document.getElementById('counter').textContent=`${String(f+1).padStart(2,'0')} / 24`;
if(document.getElementById('rig').checked&&metadata){const c=render.getContext('2d');for(const side of ['far','near']){c.strokeStyle=side==='near'?'#ffc257':'#55c9ef';c.fillStyle=c.strokeStyle;c.lineWidth=2;for(const limb of ['leg','arm']){const points=metadata.frames[f].joints[`${limb}_${side}`];c.beginPath();points.forEach(([x,y],i)=>i?c.lineTo(x,y):c.moveTo(x,y));c.stroke();for(const[x,y]of points){c.beginPath();c.arc(x,y,3,0,Math.PI*2);c.fill();}}}}}
function tick(t){const fps=Number(document.getElementById('cadence').value);if(playing&&t-last>=1000/fps){f=(f+(fps===12?2:1))%24;last=t;draw()}requestAnimationFrame(tick)}
play.onclick=()=>{playing=!playing;play.textContent=playing?'Pause':'Play'};slider.oninput=()=>{f=Number(slider.value);playing=false;play.textContent='Play';draw()};document.getElementById('reference-choice').onchange=draw;document.getElementById('resolution').onchange=draw;document.getElementById('rig').onchange=draw;requestAnimationFrame(tick);
</script></main></html>"""
    return result.replace("__PREVIOUS_OPTION__", '<option value="previous">Previous model</option>' if has_previous else '').replace(
        "__PREVIOUS_SETUP__", "const previous=new Image();previous.src='previous-sheet-512.png';previous.onload=draw;" if has_previous else '')


def project(point, manifest):
    import numpy as np
    eye, target = [np.array(manifest[key], dtype=float) for key in ("camera_location", "camera_target")]
    forward = target - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.array([0, 0, 1]))
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    relative = np.array(point) - target
    scale = 512 / manifest["orthographic_scale"]
    return [float(256 + np.dot(relative, right) * scale), float(256 - np.dot(relative, up) * scale)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--previous", type=Path)
    args = parser.parse_args()
    out = args.directory
    if (out / "sprites.json").exists():
        raise ValueError("review already exists; preserve it before regenerating")
    manifest = json.loads((out / "manifest.json").read_text())
    if args.previous:
        prior = json.loads((args.previous / "manifest.json").read_text())
        for key in ("frame_count", "resolution", "camera_location", "camera_target", "orthographic_scale"):
            if manifest[key] != prior[key]:
                raise ValueError(f"Comparison requires matching frame/camera settings: {key}")
        shutil.copyfile(args.previous / "sheet-512.png", out / "previous-sheet-512.png")
    frames = [Image.open(out / "frames" / f"run-{i:02}.png").convert("RGBA") for i in range(1, 25)]
    if any(frame.size != (512, 512) for frame in frames):
        raise ValueError("expected complete 512px render sequence")
    frame_info = []
    for index, frame in enumerate(frames):
        bounds = frame.getchannel("A").point(lambda a: 255 if a >= 128 else 0).getbbox()
        if not bounds or min(bounds[:2]) <= 1 or max(bounds[2:]) >= 511:
            raise ValueError(f"empty or clipped frame {index + 1}: {bounds}")
        joints = {}
        bones = manifest["poses"][index]["bones"]
        for side in ("near", "far"):
            for limb, names in (("leg", ("thigh", "shin", "foot")), ("arm", ("upper_arm", "forearm", "hand"))):
                chain = [bones[f"{name}.{side}"][0] for name in names] + [bones[f"{names[-1]}.{side}"][1]]
                joints[f"{limb}_{side}"] = [project(point, manifest) for point in chain]
        frame_info.append({"index": index, "bounds_512": bounds, "joints": joints,
                           "cell": [index % 6, index // 6], "duration_ms": 1000 / 24})
    variants = {512: frames, 128: pixel_frames(frames, 128), 96: pixel_frames(frames, 96)}
    for size, images in variants.items():
        pack(images).save(out / f"sheet-{size}.png")
        images[0].save(out / f"run-{size}.png", save_all=True, append_images=images[1:], duration=1000 / 24, loop=0, disposal=0, blend=0)
    gif(variants[96], out / "run-preview.gif", [40, 40, 40, 40, 40, 50] * 4, pixel=True)
    gif(frames, out / "run-smooth.gif", [40, 40, 40, 40, 40, 50] * 4)
    views = [Image.open(out / "turntable" / f"view-{i:02}.png").convert("RGBA") for i in range(16)]
    gif(views, out / "turntable.gif", 125)
    shutil.copyfile(SOURCE, out / "source.png")
    comparison = Image.new("RGBA", (1152, 424), BACKGROUND)
    draw = ImageDraw.Draw(comparison)
    for i, (label, img) in enumerate((("ORIGINAL APPEARANCE", Image.open(SOURCE).convert("RGBA")),
                                     ("RIGGED 3D MOUSE", frames[3]), ("96 PX SPRITE / ENLARGED", variants[96][3]))):
        draw.text((i * 384 + 20, 14), label, fill=(224, 207, 158), font=ImageFont.load_default(size=16))
        comparison.alpha_composite(img.resize((384, 384), Image.Resampling.NEAREST if i != 1 else Image.Resampling.LANCZOS), (i * 384, 40))
    comparison.convert("RGB").save(out / "comparison.png")
    if args.previous:
        before_after = Image.new("RGBA", (1024, 562), BACKGROUND)
        labels = ImageDraw.Draw(before_after)
        for index, (label, path) in enumerate((("PREVIOUS VERSION", args.previous / "hero.png"),
                                                ("REFINED VERSION", out / "hero.png"))):
            labels.text((index*512+26, 17), label, fill=(224, 207, 158), font=ImageFont.load_default(size=19))
            before_after.alpha_composite(Image.open(path).convert("RGBA"), (index*512, 50))
        before_after.convert("RGB").save(out / "before-after.png")
    (out / "review.html").write_text(html(bool(args.previous)))
    shutil.copyfile(Path(__file__), out / "source" / Path(__file__).name)
    info = {"frame_count": 24, "fps": 24, "columns": 6, "rows": 4, "frame_sizes": [512, 128, 96],
            "camera": {key: manifest[key] for key in ("camera_location", "camera_target", "orthographic_scale")},
            "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(), "frames": frame_info,
            "postprocess": "Lanczos reduction, shared 48-color palette per resolution, alpha threshold 128, one-pixel dark outline",
            "sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in sorted(out.glob("sheet-*.png"))}}
    (out / "sprites.json").write_text(json.dumps(info, indent=2) + "\n")
    print(f"Packed {len(frames)} frames at 512/128/96px; all silhouettes have transparent margins: {out}")


if __name__ == "__main__":
    main()
