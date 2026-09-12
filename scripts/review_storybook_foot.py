"""Package a fixed-camera foot study with unchanged original/revised framing."""

import argparse
import json
from pathlib import Path
import shutil

from PIL import Image, ImageDraw, ImageFont

from review_storybook_run import bounds, flat, sheet


BACKGROUND = (28, 38, 32)
COLORS = {'thigh': '#efb966', 'shin': '#7bcadd', 'foot': '#ef8d89', 'toe': '#afd98a'}


def joints(frame, pixels):
    result = frame.copy()
    draw = ImageDraw.Draw(result)
    for part, color in COLORS.items():
        name = f'{part}.left'
        if name not in pixels:
            continue
        a, b = pixels[name]
        draw.line((*a, *b), fill=color, width=3)
        for x, y in (a, b):
            draw.ellipse((x-3, y-3, x+3, y+3), fill=color)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('result', type=Path)
    args = parser.parse_args()
    out = args.result
    if (out/'review.json').exists():
        raise ValueError('Preserve the previous package before repackaging')
    data = json.loads((out/'study.json').read_text())
    font = ImageFont.load_default(size=18)
    comparisons = Image.new('RGB', (1536, 1120), BACKGROUND)
    draw = ImageDraw.Draw(comparisons)
    for column, view in enumerate(('front', 'side', 'game')):
        for row, version in enumerate(('original', 'revised')):
            key = f'{version}-{view}'
            frame = Image.open(out/f'{key}.png').convert('RGBA')
            comparisons.paste(flat(frame), (column*512, row*560+36))
            draw.text((column*512+24, row*560+14), f'{version.title()} / {view}', font=font, fill='#e7dfc6')
    comparisons.save(out/'comparison.png')
    # A common side crop enlarges the changed region without fitting silhouettes.
    details = Image.new('RGB', (960, 560), BACKGROUND)
    draw = ImageDraw.Draw(details)
    for column, version in enumerate(('original', 'revised')):
        frame = Image.open(out/f'{version}-side.png').convert('RGBA')
        frame = joints(flat(frame), data['comparison_pixels'][f'{version}-side'])
        crop = frame.crop((190, 300, 370, 485)).resize((432, 444), Image.Resampling.LANCZOS)
        details.paste(crop, (column*480+24, 65))
        draw.text((column*480+24, 20), version.title()+' / lower leg + foot', font=font, fill='#e7dfc6')
    draw.text((24, 530), 'Thigh: gold   Shin: blue   Raised foot: coral   Toes: green', font=font, fill='#e7dfc6')
    details.save(out/'foot-comparison.png')
    metadata = []
    all_frames = {}
    for view in ('side', 'game'):
        frames = [Image.open(out/f'frames-{view}'/f'pose-{i:02}.png').convert('RGBA') for i in range(1, 34)]
        all_frames[view] = frames
        for index, frame in enumerate(frames):
            box = bounds(frame)
            if frame.size != (512, 512) or not box or min(box[:2]) <= 0 or max(box[2:]) >= 512:
                raise ValueError(f'{view} frame {index+1} is empty or clipped')
            metadata.append({'view': view, 'frame': index+1, 'bounds_512': box})
        for size in (96, 128, 512):
            resized = [f.resize((size, size), Image.Resampling.LANCZOS) for f in frames]
            sheet(resized, 6).save(out/f'{view}-{size}.png')
        animated = [flat(f.resize((384, 384), Image.Resampling.LANCZOS)) for f in frames]
        # Endpoint holds make the discontinuous reset explicit; this is not a looped run.
        animated[0].save(out/f'{view}-study.gif', save_all=True, append_images=animated[1:],
                          duration=[550]+[70]*31+[800], loop=0, disposal=2)
    poses = Image.new('RGB', (1280, 580), BACKGROUND)
    draw = ImageDraw.Draw(poses)
    for column, (frame, name) in enumerate(data['poses'].items()):
        index = int(frame)-1
        poses.paste(flat(all_frames['game'][index].resize((256, 256), Image.Resampling.LANCZOS)), (column*256, 32))
        detail = joints(flat(all_frames['side'][index]), data['side_pixels'][frame])
        detail = detail.crop((130, 285, 415, 485)).resize((256, 180), Image.Resampling.LANCZOS)
        poses.paste(detail, (column*256, 320))
        draw.text((column*256+12, 8), name, font=ImageFont.load_default(size=16), fill='#e7dfc6')
    draw.text((20, 535), 'Five blocked poses / fixed game camera above, side-view leg chains below', font=font, fill='#e7dfc6')
    poses.save(out/'poses.png')
    checks = data['checks']
    length_error = max(abs(sum((a-b)**2 for a, b in zip(*points))**.5-data['rest_bones'][name]['length'])
                       for check in checks for name, points in check['bones'].items())
    summary = {'status': 'awaiting_visual_review', 'saved_copy_verified': data['saved_copy_verified'],
               'changed_vertices': len(data['changed_vertex_indices']),
               'changed_weights': len(data['changed_weight_indices']), 'changed_bones': data['changed_bones'],
               'lowest_z': min(c['lowest_z'] for c in checks),
               'max_bone_length_error': length_error,
               'max_support_toe_height_error': max(abs(c['toe_lowest_z'][s]) for c in checks
                                                   for s in ('left', 'right') if s == 'right' or c['frame'] <= 25),
               'frames': metadata, 'poses': data['poses'], 'ground_row_512': data['ground_row_512'],
               'game_pixels': {str(int(c['frame'])): c['pixels'] for c in checks if c['frame'].is_integer()},
               'side_pixels': data['side_pixels']}
    (out/'review.json').write_text(json.dumps(summary, indent=2)+'\n')
    template = Path(__file__).with_name('storybook_foot_review.html').read_text()
    (out/'review.html').write_text(template.replace('__STUDY_DATA__', json.dumps(summary)))
    for source in (Path(__file__), Path(__file__).with_name('storybook_foot_review.html'),
                   Path(__file__).with_name('review_storybook_run.py')):
        shutil.copyfile(source, out/source.name)
    print(json.dumps({k: v for k, v in summary.items() if k not in ('frames', 'game_pixels', 'side_pixels')}))


if __name__ == '__main__':
    main()
