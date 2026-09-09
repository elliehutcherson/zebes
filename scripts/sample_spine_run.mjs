// Sample the published example with the official runtime in an isolated build directory.
// No textures, rendering, networking, engine integration, or generation calls.
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';

const [runtimeDirectory, input, output] = process.argv.slice(2);
if (!runtimeDirectory || !input || !output) {
  throw new Error('Usage: node scripts/sample_spine_run.mjs RUNTIME_DIRECTORY INPUT_JSON OUTPUT_JSON');
}
const packageInfo = JSON.parse(fs.readFileSync(path.join(runtimeDirectory, 'package.json')));
if (packageInfo.name !== '@esotericsoftware/spine-core' || packageInfo.version !== '4.2.120') {
  throw new Error('This comparison requires @esotericsoftware/spine-core 4.2.120');
}
const spine = await import(pathToFileURL(path.resolve(runtimeDirectory, 'dist/index.js')));
const bytes = fs.readFileSync(input);
const source = JSON.parse(bytes);
if (!source.skeleton.spine.startsWith('4.2.') || source.path?.length || source.physics?.length) {
  throw new Error('Expected a 4.2 example without path or physics constraints');
}
// Attachment loaders may return null. Keep all slots, run timelines and
// constraints; only image attachments are omitted in this skeleton-only study.
// Other clips have mesh-deform timelines that require those image attachments.
if (!source.animations.run || source.animations.run.attachments) {
  throw new Error('Expected a run clip without attachment deformation');
}
const loader = Object.fromEntries([
  'newRegionAttachment', 'newMeshAttachment', 'newBoundingBoxAttachment',
  'newPathAttachment', 'newPointAttachment', 'newClippingAttachment',
].map(name => [name, () => null]));
const data = new spine.SkeletonJson(loader).readSkeletonData({...source, animations: {run: source.animations.run}});
const animation = data.findAnimation('run');
if (!animation || !(animation.duration > 0)) throw new Error('No nonempty run animation');
const skeleton = new spine.Skeleton(data);
const mapping = {
  hip_c: 'hip', neck: 'neck', head_top: 'head',
  shoulder_l: 'front-upper-arm', elbow_l: 'front-bracer', wrist_l: 'front-fist',
  shoulder_r: 'rear-upper-arm', elbow_r: 'rear-bracer', wrist_r: 'gun',
  knee_l: 'front-shin', ankle_l: 'front-foot', toe_l: 'front-foot-tip',
  knee_r: 'rear-shin', ankle_r: 'rear-foot', toe_r: 'back-foot-tip',
};
function point(name, tip = false) {
  const bone = skeleton.findBone(name);
  if (!bone) throw new Error(`Missing required example bone: ${name}`);
  const x = bone.worldX + (tip ? bone.a * bone.data.length : 0);
  const y = bone.worldY + (tip ? bone.c * bone.data.length : 0);
  if (!Number.isFinite(x) || !Number.isFinite(y)) throw new Error(`Invalid bone: ${name}`);
  return [x, -y]; // Screen convention, x right, y down. No fitting or registration.
}
function sample(time) {
  skeleton.setToSetupPose();
  // Do not wrap: evaluating the endpoint must measure the authored closing
  // keys, rather than trivially comparing time zero to wrapped time zero.
  animation.apply(skeleton, -1, time, false, [], 1, spine.MixBlend.setup, spine.MixDirection.mixIn);
  skeleton.updateWorldTransform(spine.Physics.none);
  const pose = Object.fromEntries(Object.entries(mapping).map(([key, bone]) => [key, point(bone, key === 'head_top')]));
  pose.paw_l = point('front-fist', true);
  pose.paw_r = point('gun'); // Wrist marker only: the weapon's tip is not a hand.
  pose.foot_tip_l = point('front-foot-tip', true);
  pose.foot_tip_r = point('back-foot-tip', true);
  pose.hip_l = point('front-thigh');
  pose.hip_r = point('rear-thigh');
  return pose;
}
const frames = Array.from({length: 12}, (_, index) => ({
  name: `spine_${String(index + 1).padStart(2, '0')}`,
  time_seconds: animation.duration * index / 12,
  pose: sample(animation.duration * index / 12),
}));
const endpoint = sample(animation.duration);
const closureError = Math.max(...Object.keys(endpoint).map(key => Math.hypot(
  endpoint[key][0] - frames[0].pose[key][0], endpoint[key][1] - frames[0].pose[key][1])));
if (new Set(frames.map(frame => JSON.stringify(frame.pose))).size !== 12) {
  throw new Error('The run did not produce twelve distinct samples');
}
const result = {
  version: 1, status: 'external example comparison; not accepted mouse motion',
  source_url: 'https://raw.githubusercontent.com/EsotericSoftware/spine-runtimes/4.2/examples/spineboy/export/spineboy-pro.json',
  source_sha256: createHash('sha256').update(bytes).digest('hex'),
  source_export_version: source.skeleton.spine,
  runtime: `${packageInfo.name}@${packageInfo.version}`,
  evaluation_reference: 'https://en.esotericsoftware.com/spine-download',
  duration_seconds: animation.duration, sampled_frames: 12,
  endpoint_error: closureError, endpoint_evaluation: 'non-wrapped authored endpoint, original rig units', mapping,
  notes: 'Official run, original timing and proportions. No per-frame fitting. Includes a weapon-holding arm. The closing endpoint is checked but not exported as a thirteenth frame.',
  frames,
};
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, JSON.stringify(result, null, 2) + '\n');
console.log(`Sampled twelve distinct phases over ${animation.duration.toFixed(3)} seconds; endpoint error ${closureError}.`);
