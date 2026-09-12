"""Package Blender's fixed-camera run renders; no cell fitting or repositioning."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil

from PIL import Image, ImageDraw, ImageFont


BACKGROUND = (28, 38, 32, 255)


def sheet(frames, columns):
    size = frames[0].width
    out = Image.new('RGBA', (columns*size, math.ceil(len(frames)/columns)*size))
    for index, frame in enumerate(frames):
        out.paste(frame, ((index%columns)*size, (index//columns)*size))
    return out


def bounds(frame):
    return frame.getchannel('A').point(lambda value: 255 if value >= 128 else 0).getbbox()


def flat(frame):
    out = Image.new('RGBA', frame.size, BACKGROUND)
    out.alpha_composite(frame)
    return out.convert('RGB')


def durations(count, seconds, quantum=1):
    """Round frame boundaries to the format's millisecond time resolution."""
    if count <= 0 or not math.isfinite(seconds) or seconds <= 0 or quantum <= 0:
        raise ValueError('Frame count, cycle duration and time resolution must be positive')
    result = [quantum*(round((i+1)*1000*seconds/count/quantum)
                       - round(i*1000*seconds/count/quantum)) for i in range(count)]
    if min(result) <= 0:
        raise ValueError('Cycle is too short for this frame count and time resolution')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('result', type=Path)
    args = parser.parse_args()
    out = args.result
    if (out/'review.json').exists():
        raise ValueError('Preserve the existing review before repackaging')
    report = json.loads((out/'motion.json').read_text())
    masters = [Image.open(out/'frames-512'/f'run-{index:02}.png').convert('RGBA') for index in range(24)]
    metadata = []
    for index, frame in enumerate(masters):
        box = bounds(frame)
        if frame.size != (512, 512) or not box or min(box[:2]) <= 0 or max(box[2:]) >= 512:
            raise ValueError(f'Invalid or clipped source render {index}')
        metadata.append({'index': index, 'blender_frame': index+1, 'phase': index/24,
                         'time_seconds': index/report['fps'], 'bounds_512': box,
                         'origin_512': [256, report['camera']['ground_row_512']],
                         'sheet_cell': [index%6, index//6]})
    for size in (96, 128, 512):
        frames = masters if size == 512 else [f.resize((size, size), Image.Resampling.LANCZOS) for f in masters]
        if size < 512:
            (out/f'frames-{size}').mkdir()
            for index, frame in enumerate(frames):
                frame.save(out/f'frames-{size}'/f'run-{index:02}.png')
        sheet(frames, 6).save(out/f'sprites-{size}.png')
        sheet(frames[::2], 6).save(out/f'sprites-{size}-12.png')
        if size < 512:
            for step in (1, 2):
                subset = frames[::step]
                subset[0].save(out/f'run-{size}-{len(subset)}.apng', save_all=True,
                               append_images=subset[1:], duration=durations(len(subset), report['cycle_seconds']),
                               loop=0, disposal=0, blend=0)
    for size in (128, 384):
        frames = [flat(f.resize((size, size), Image.Resampling.LANCZOS)) for f in masters]
        palette = sheet([f.convert('RGBA') for f in frames], 6).convert('RGB').quantize(colors=255)
        frames = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
        frames[0].save(out/f'run-{size}.gif', save_all=True, append_images=frames[1:],
                       duration=durations(len(frames), report['cycle_seconds'], 10), loop=0, disposal=2)
    contact_sheet = Image.new('RGB', (1024, 560), BACKGROUND[:3])
    draw = ImageDraw.Draw(contact_sheet)
    labels = ['Near contact', 'Compression', 'Passing / recovery', 'Flight',
              'Far contact', 'Compression', 'Passing / recovery', 'Flight']
    if report.get('revision') == 2:
        labels = ['Near toe contact', 'Heel compression', 'Toe push-off', 'Flight',
                  'Far toe contact', 'Heel compression', 'Toe push-off', 'Flight']
    for index, frame_index in enumerate(range(0, 24, 3)):
        x, y = index%4*256, index//4*280
        contact_sheet.paste(flat(masters[frame_index].resize((256, 256))), (x, y))
        draw.text((x+12, y+255), f'{frame_index+1:02}  {labels[index]}', fill=(216, 222, 198),
                  font=ImageFont.load_default(size=13))
    contact_sheet.save(out/'contact-sheet.png')
    review = {'status': 'awaiting_user_run_verdict', 'frames': metadata,
              'revision': report.get('revision', 1), 'stride': report['stride'],
              'contact_marker': report.get('contact_marker', 'sole'),
              'cycle_seconds': report['cycle_seconds'], 'full_fps': report['fps'], 'sparse_fps': report['fps']/2,
              'sparse_source_indices': list(range(0, 24, 2)),
              'preview_method': 'Whole-frame Lanczos reduction; RGBA retained; no per-frame fitting',
              'camera': report['camera'], 'ground_z': 0,
              'checks': report['checks'][::8][:-1],
              'source_unchanged': report['before'] == report['after'],
              'max_stance_sole_error_units': max(abs(p['lowest_z']) for c in report['checks']
                                                for p in c['paws'].values() if p['stance']),
              'max_length_error_units': max(abs(math.dist(*points)-report['rest_bones'][name]['length'])
                                            for c in report['checks'] for name, points in c['bones'].items()),
              'loop_joint_error_units': max(math.dist(a, b) for name in report['checks'][0]['bones']
                                            for a, b in zip(report['checks'][0]['bones'][name], report['checks'][-1]['bones'][name]))}
    if report.get('revision') == 2:
        stance = [p for c in report['checks'] for p in c['paws'].values() if p['stance']]
        review['max_stance_pad_error_units'] = max(abs(p['markers']['pad'][2]) for p in stance)
        review['min_stance_heel_height_units'] = min(p['markers']['heel'][2] for p in stance)
    template = Path(__file__).with_name('storybook_run_review.html').read_text()
    if report.get('revision') == 2:
        template = (template.replace('Run 01', 'Run 02').replace('A running mouse', 'A quicker, toe-led run')
                    .replace('two-thirds-of-a-second', 'half-second')
                    .replace('36 fps', '48 fps').replace('18 fps', '24 fps')
                    .replace('three steps', 'four steps').replace('Planted sole', 'Planted toe pad'))
    (out/'review.html').write_text(template.replace('__REVIEW_DATA__', json.dumps(review)))
    files = [p for p in out.iterdir() if p.is_file() and p.suffix in ('.png', '.gif', '.apng', '.blend', '.json', '.html')]
    files += sorted(out.glob('frames-*/*.png'))
    review['sha256'] = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    (out/'review.json').write_text(json.dumps(review, indent=2)+'\n')
    for source in (Path(__file__), Path(__file__).with_name('storybook_run_review.html')):
        shutil.copyfile(source, out/source.name)
    print(json.dumps({k: review[k] for k in ('status', 'source_unchanged', 'max_stance_sole_error_units',
                                            'max_length_error_units', 'loop_joint_error_units')}))


if __name__ == '__main__':
    main()
