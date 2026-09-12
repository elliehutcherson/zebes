# Coherent-sheet animation gate — 2026-08-29

Historical result. Current production contracts live in the
[frame-set pipeline](../animation-artwork-pipeline.md).

The disposable implementation and bounded image requests have now exercised
this contract. Evidence remains under the ignored
`build/animation-feasibility/` tree; these digests make the result identifiable
without turning disposable images into checked-in production assets.

| Input | Result | Source RGBA digest | Packed RGBA digest |
|---|---|---|---|
| Manual ten-frame locomotion | Structural pass; independent `deterministic_payload` rerun matched exactly. | `f2fef6e3ae458fed09df764c621515b04fc073aee290e6c2600ff8cad799127a` | `4e1eb46bcc0783e49471559b803f4396b3d90aa047064985ff472fdcb49c9964` |
| Manual four-frame idle | Structural pass; independent `deterministic_payload` rerun matched exactly. | `3ec2b6319992b8a58f9c45edf3b288106e341ad91d83f8b3d1ba1ef028411e95` | `09b8fa3b55656dda715b4507b226e43b8050c1cfe5ab947bbc22d51e820f1c55` |
| Generated locomotion attempt 2 | Automated structural pass and independent deterministic rerun match, but live Catacombs playback is a hard failure. The frames do not form fluid motion or read as a person running; the coherent-sheet request did not follow the pose guide closely enough. | `3d6b494788f5fc99fd4eb192df6d76046f4de1ce56369c09786da8b705aa24e3` | `14866473aceb6759d47842003a06c935af0750fef504cdb56c1f357c6463d947` |
| Generated idle attempt 2 | Automated pass but visible-review failure: column-dependent registration produces an approximately three-native-pixel left/right oscillation, and adjacent changes cover roughly the whole character rather than a subtle breathing motion. | `ced67b64d4b9b14c3eb674b13278edbbef6ab37ea12bc5234497d38ae4632edd` | `5fe1c59237d1dc6c6e3bba5364b1db47f4e705d6a066d0efe4ec339e854eceec` |

The request count reached the locked maximum: one identity-board request, two
locomotion requests, and two idle requests. Locomotion attempt 1 and idle
attempt 1 had no valid equal-square-cell extraction and failed before
processing. Attempt 2 used canvas-matched pose boards; locomotion then admitted
one shared `256 × 256` extraction with a 28-pixel row gap, while idle required a
shared `400 × 400` extraction but failed visible registration and motion review.
No frame was regenerated or repaired independently.

Live review on 2026-08-29 overturned the still-frame impression of locomotion
attempt 2. Structural checks, per-frame inspection, and difference images did
not establish temporal coherence: at speed the sequence is visibly discontinuous
and does not communicate a run. This is the decisive gate observation, not a
polish issue and not something the deterministic processor may repair.

The full generated-source gate is a hard fail. Coherent whole-sheet generation
is not a feasible animation source with this technique. Keep imported/manual
sheets as the production path, do not add provider-specific animation transport
to this locked run, and do not spend further requests on it.

The separately budgeted
[pose-conditioned experiment](animation-pose-conditioned-experiment.md)
also failed. One composite identity board and three separated identity views
both produced dimensionally different characters, and the opposing guides did
not produce unambiguous pose phases. That complete experiment is deprecated.
No parametric-guide, sequential-conditioning, generated-sheet, independent-frame
batch, or additional provider call is active roadmap work.

Remote provider success is not a prerequisite for animation authoring. The
production path is imported or manually authored sheets through source-neutral
processing, review, recipe, and transactional persistence. Generated animation
research cannot be used to delay or redefine that path.
