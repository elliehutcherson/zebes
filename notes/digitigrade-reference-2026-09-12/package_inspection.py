"""Make an attributed review from measured third-party poses and local renders."""

import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
out = root/'run-inspection-wide'
samples = json.loads((out/'sampled-run.json').read_text())['samples']
font = ImageFont.load_default(size=18)
small = ImageFont.load_default(size=14)
frames = []
for index in range(16):
    page = Image.new('RGB', (1000, 500), '#202b29')
    draw = ImageDraw.Draw(page)
    render = Image.open(out/f'run-{index:02}.png').convert('RGBA')
    page.paste(render.resize((680, 340)), (0, 80), render.resize((680, 340)))
    draw.text((22, 16), 'Downloaded run: Low Spec Velociraptor / pistachio / CC BY 4.0', font=font, fill='#f0e8d0')
    draw.text((22, 47), f'Original run, frame {index:02} of the advertised 16-frame cycle, 30 fps', font=small, fill='#abbeb8')
    draw.text((22, 462), 'Diagnostic render only: flat color and side camera added; source rig/action unchanged.', font=small, fill='#abbeb8')
    draw.text((708, 85), 'Left leg / side projection', font=font, fill='#f0e8d0')
    bones = samples[index]['bones']
    point = lambda p: (840+p[1]*350, 440-p[2]*350)
    colors = ['#e8ba72', '#8bc7e8', '#f28e8e', '#a6d78c']
    for name, color, label in zip(('thigh.l', 'leg1.l', 'leg2.l', 'foot.l'), colors,
                                 ('Thigh', 'Shin', 'Raised foot segment', 'Toes')):
        a, b = map(point, bones[name])
        draw.line([a, b], fill=color, width=5)
        draw.ellipse((a[0]-4, a[1]-4, a[0]+4, a[1]+4), fill=color)
    for i, (color, label) in enumerate(zip(colors, ('Thigh', 'Shin', 'Raised foot segment', 'Toes'))):
        draw.text((705, 120+i*22), label, fill=color, font=small)
    frames.append(page)
palette = frames[0].quantize(colors=255)
gif_frames = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
durations = [10*(round((i+1)*100/30)-round(i*100/30)) for i in range(16)]
gif_frames[0].save(out/'run-rig-review.gif', save_all=True, append_images=gif_frames[1:],
                   duration=durations, loop=0)
sheet = Image.new('RGB', (1000, 2000), '#202b29')
for i, index in enumerate((0, 4, 8, 12)):
    sheet.paste(frames[index], (0, i*500))
sheet.save(out/'pose-comparison.png')
def angle(a, b):
    dot = sum(x*y for x, y in zip(a, b))
    length = math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))
    return math.degrees(math.acos(max(-1, min(1, dot/length))))
measurements = {}
for parent, child in (('thigh.l', 'leg1.l'), ('leg1.l', 'leg2.l'), ('leg2.l', 'foot.l')):
    angles = []
    for sample in samples[:16]:
        pa, pb = sample['bones'][parent]
        ca, cb = sample['bones'][child]
        angles.append(angle([b-a for a,b in zip(pa,pb)], [b-a for a,b in zip(ca,cb)]))
    measurements[f'{parent} -> {child}'] = {'min_degrees_between_segments': min(angles),
                                           'max_degrees_between_segments': max(angles)}
(out/'joint-motion-summary.json').write_text(json.dumps(measurements, indent=2)+'\n')
print(json.dumps(measurements))
