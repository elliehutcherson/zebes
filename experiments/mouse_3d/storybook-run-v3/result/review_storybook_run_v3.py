"""Package run 03's two cameras and a locally retained, attributed reference."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil

from PIL import Image, ImageDraw, ImageFont

from review_storybook_run import bounds, flat, sheet

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT/'notes/digitigrade-reference-2026-09-12/run-inspection-wide'


def durations(count, seconds, quantum=1):
    return [quantum*(round((i+1)*1000*seconds/count/quantum)-round(i*1000*seconds/count/quantum))
            for i in range(count)]


def gif(frames, path, seconds):
    palette = sheet([f.resize((128, 128)).convert('RGBA') for f in frames], 6).convert('RGB').quantize(colors=255)
    indexed = [f.convert('RGB').quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    indexed[0].save(path, save_all=True, append_images=indexed[1:],
                    duration=durations(len(frames), seconds, 10), loop=0, disposal=2)


def package(out):
    if (out/'review.json').exists():
        raise ValueError('Preserve existing review; use a new output directory')
    report = json.loads((out/'motion.json').read_text())
    seconds = report['cycle_seconds']
    checks = report['checks'][::(len(report['checks'])-1)//24][:-1]
    metadata, all_masters = [], {}
    for view in ('game', 'side'):
        masters = [Image.open(out/f'frames-{view}'/f'run-{i:02}.png').convert('RGBA') for i in range(24)]
        all_masters[view] = masters
        for i, frame in enumerate(masters):
            box = bounds(frame)
            if frame.size != (512, 512) or not box or min(box[:2]) <= 0 or max(box[2:]) >= 512:
                raise ValueError(f'Invalid or clipped {view} frame {i}')
            metadata.append({'view': view, 'index': i, 'blender_frame': i+1, 'phase': i/24,
                             'time_seconds': i/45, 'bounds_512': box, 'sheet_cell': [i%6, i//6],
                             'origin_512': [256, report['camera']['ground_row_512']]})
        for size in (96, 128, 512):
            frames = [f.resize((size, size), Image.Resampling.LANCZOS) for f in masters]
            sheet(frames, 6).save(out/f'{view}-{size}.png')
            sheet(frames[::2], 6).save(out/f'{view}-{size}-12.png')
            if size < 512:
                for step in (1, 2):
                    subset = frames[::step]
                    subset[0].save(out/f'{view}-{size}-{len(subset)}.apng', save_all=True,
                                   append_images=subset[1:], duration=durations(len(subset), seconds),
                                   loop=0, disposal=0, blend=0)
        for size in (128, 384):
            gif([flat(f.resize((size, size), Image.Resampling.LANCZOS)) for f in masters],
                out/f'{view}-{size}.gif', seconds)
    reference = [Image.open(REFERENCE/f'run-{i:02}.png').convert('RGBA') for i in range(16)]
    reference_sheet = Image.new('RGBA', (4*512, 4*256))
    for i, frame in enumerate(reference):
        reference_sheet.paste(frame.resize((512, 256), Image.Resampling.LANCZOS), (i%4*512, i//4*256))
    reference_sheet.save(out/'reference.png')
    attribution = {'title': 'Low Spec Velociraptor', 'creator': 'pistachio', 'license': 'CC BY 4.0',
                   'source_url': 'https://opengameart.org/content/low-spec-velociraptor',
                   'license_url': 'https://creativecommons.org/licenses/by/4.0/',
                   'modifications': 'Diagnostic side camera and flat material; original rig and action unchanged.',
                   'samples': 16, 'fps': 30, 'cycle_seconds': 16/30,
                   'source_frame_sha256': {f'run-{i:02}.png': hashlib.sha256((REFERENCE/f'run-{i:02}.png').read_bytes()).hexdigest()
                                           for i in range(16)}}
    (out/'reference-attribution.json').write_text(json.dumps(attribution, indent=2)+'\n')
    contact = Image.new('RGB', (1024, 560), '#1c2620')
    draw = ImageDraw.Draw(contact)
    font = ImageFont.load_default(size=14)
    for i, index in enumerate(range(0, 24, 3)):
        x, y = i%4*256, i//4*280
        contact.paste(flat(all_masters['game'][index].resize((256, 256))), (x, y))
        draw.text((x+10, y+258), f'{index+1:02} / {"Support" if any(p["stance"] for p in checks[index]["paws"].values()) else "Flight"}',
                  fill='#ece7ce', font=font)
    contact.save(out/'contact-sheet.png')
    comparison = []
    for i in range(48):
        page = Image.new('RGB', (900, 512), '#1c2620')
        page.paste(flat(all_masters['game'][i//2].resize((448, 448))), (0, 35))
        ref = reference[i//3].resize((448, 224), Image.Resampling.LANCZOS)
        page.paste(ref, (450, 160), ref)
        d = ImageDraw.Draw(page)
        d.text((20, 15), 'Mouse / Run 03 / normal speed', fill='#eee6cd', font=font)
        d.text((460, 15), 'Low Spec Velociraptor / pistachio / CC BY 4.0', fill='#eee6cd', font=font)
        d.text((20, 489), '24 samples at 45fps / 0.533s', fill='#bacbbd', font=font)
        d.text((460, 463), 'opengameart.org/content/low-spec-velociraptor', fill='#bacbbd', font=font)
        d.text((460, 489), '16 samples at 30fps / 0.533s / diagnostic render', fill='#bacbbd', font=font)
        comparison.append(page)
    gif(comparison, out/'mouse-and-raptor.gif', seconds)
    first_draft = out.parent/'draft-02'
    if first_draft != out:
        comparison = []
        for i,frame in enumerate(all_masters['game']):
            page=Image.new('RGB',(768,420),'#1c2620')
            old=Image.open(first_draft/'frames-game'/f'run-{i:02}.png').convert('RGBA')
            page.paste(flat(old.resize((384,384))), (0,30))
            page.paste(flat(frame.resize((384,384))), (384,30))
            d=ImageDraw.Draw(page)
            d.text((16,8),'First preview / 0.533s',fill='#d9ccaa',font=font)
            d.text((400,8),'Revised stride, lean and fists / 0.533s',fill='#d9ccaa',font=font)
            comparison.append(page)
        gif(comparison,out/'revision-comparison.gif',seconds)
    baseline = out.parent/'weight-baseline'
    if baseline.exists():
        old_report=json.loads((baseline/'motion.json').read_text())
        deformation={'baseline': 'weight-baseline', 'same_leg_poses': True,
                     'before_max_hock_edge_ratio': max(c['hock_edge_ratio']['max'] for c in old_report['checks']),
                     'after_max_hock_edge_ratio': max(c['hock_edge_ratio']['max'] for c in report['checks'])}
        for old,current in zip(old_report['checks'],report['checks'][::(len(report['checks'])-1)//48]):
            for name in old['bones']:
                if name.startswith(('upper_arm.','forearm.','hand.','tail.')):
                    continue
                if max(math.dist(a,b) for a,b in zip(old['bones'][name],current['bones'][name])) > 1e-5:
                    raise ValueError('Weight comparison uses different lower-body poses')
        (out/'deformation-comparison.json').write_text(json.dumps(deformation,indent=2)+'\n')
        diagnostic=Image.new('RGB',(1024,640),'#1c2620')
        d=ImageDraw.Draw(diagnostic)
        for col,index in enumerate((3,8,15,20)):
            for row,directory in enumerate((baseline,out)):
                frame=Image.open(directory/'frames-side'/f'run-{index:02}.png').convert('RGBA')
                # One common diagnostic crop only; sprite exports remain full-frame.
                crop=flat(frame.crop((180,270,420,500))).resize((256,256),Image.Resampling.LANCZOS)
                diagnostic.paste(crop,(col*256,row*320+32))
                d.text((col*256+8,row*320+8),f'{"Study weights" if row==0 else "Repaired weights"} / pose {index+1:02}',
                       fill='#e2d4b2',font=font)
        diagnostic.save(out/'deformation-comparison.png')
    summary = {'max_ground_penetration_units': max(0, -min(c['lowest_z'] for c in report['checks'])),
               'max_stance_toe_error_units': max(abs(p['lowest_z']) for c in report['checks']
                                                 for p in c['paws'].values() if p['stance']),
               'max_bone_length_error': max(abs(math.dist(*points)-report['rest_bones'][name]['length'])
                                             for c in report['checks'] for name, points in c['bones'].items()),
               'loop_joint_error': max(math.dist(a,b) for name in checks[0]['bones']
                                       for a,b in zip(checks[0]['bones'][name],report['checks'][-1]['bones'][name]))}
    review = {'status': 'awaiting_user_run_verdict', 'cycle_seconds': seconds, 'fps': 45,
              'frames': metadata, 'checks': checks, 'side_pixels': report['side_pixels'],
              'side_paw_pixels': report.get('side_paw_pixels'),
              'camera': report['camera'], 'stride': report['stride'], 'summary': summary,
              'reference': attribution, 'measurements': len(report['checks']),
              'preview_method': 'Whole-frame Lanczos reduction; no per-frame fitting or repositioning'}
    template = Path(__file__).with_name('storybook_run_v3_review.html')
    html = template.read_text().replace('__REVIEW_DATA__', json.dumps(review))
    html = html.replace('__VIRTUAL_SPEED__', f'{report["virtual_forward_speed"]:.3f}')
    (out/'review.html').write_text(html)
    (out/'review.json').write_text(json.dumps(review, indent=2)+'\n')
    for source in (Path(__file__), Path(__file__).with_name('review_storybook_run.py'), template):
        shutil.copyfile(source, out/source.name)
    hashes = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(out.rglob('*')) if p.is_file() and p.name != 'artifact-sha256.json'}
    (out/'artifact-sha256.json').write_text(json.dumps(hashes, indent=2)+'\n')
    print(json.dumps(summary))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('result', type=Path)
    package(parser.parse_args().result)
