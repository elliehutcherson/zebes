# Completed ComfyUI connection control

Twelve requests completed on derry's RTX 3090 on 2026-09-09. The user reviewed
`run-gap-inputs-v1`, considered trying the connection worthwhile, and then
authorized continued experiments. This trial uses those inputs unchanged.

Open `review.html` to compare inputs and results for poses 5 and 10 at denoise
0.35 and 0.60. Each setting uses blank, blurred and shaped input variants with
the same seed, mask, prompt and model settings. All outputs are 1024×1024.
`summary.json` records measurements and the decision. The review also contains
the separately prepared boot-view guides; those have **not** been submitted.

## Finding

The prefill supplies most of the useful connection. The blank inputs retain
substantial gaps. Blur and shaped inputs stay mostly filled, but the sampler
does not turn them into convincingly finished lower-leg artwork. Higher denoise
sometimes reopens small light regions. At the fixed 1024→256→48 nearest-neighbor
mapping, outputs change only 3–11 pixels beyond their respective prepared
inputs. These counts measure local change, not animation quality.

In the proposed gap region, pose 10's blank input contains 74 near-white working
pixels; 70 remain after denoise 0.35 and 42 after 0.60. Both prefills start at
zero; the shaped result has zero at 0.35 and four at 0.60. Pose 5's blank input
starts at 60 and ends at 51/44. These are illustrative region diagnostics,
not proof that a connection is anatomically correct.

No full twelve-frame generation batch was run. The control protects most boot
pixels, so it cannot address the user's changing-view observation. Its results
do not reject generation of an entire leg and boot. The four reserved pilot
checks and full-cycle allowance remain unused.

## Execution and provenance

Each directory retains the submitted API graph, prompt ID/receipt, full ComfyUI
history, untouched raw output, server composite and a locally restored
`composite.png`. Every local composite has exactly zero changed pixels outside
the reviewed mask. The server composite comparison is recorded separately.
`working.png` and `native-48.png` are deterministic whole-canvas previews; no
figure cropping, repositioning or per-frame scale adjustment is performed.
Outputs use a white matte; no production alpha/import result is claimed.

The original graphs and input hashes are retained in `run-gap-inputs-v1`.
Uploads were byte-verified against those hashes. An initial upload verification
used the wrong field (`name` instead of `filename`) for `/view`; it failed before
any prompt was submitted. That was fixed before the twelve successful requests.
The runner never retries an ambiguous prompt submission, and an existing prompt
ID resumes polling rather than creating a duplicate job.

The first request took approximately 34.9 seconds including model loading and
polling. Subsequent requests took about 10.2–10.3 seconds each. Summed request
wall time was 148.1 seconds. This is not peak-VRAM instrumentation; receipts
contain ComfyUI's before/after system snapshots. The shared installation was
not upgraded and no new model weights were downloaded.

## Reproduction

Open a loopback-only tunnel:

```bash
ssh -o BatchMode=yes -o ExitOnForwardFailure=yes -N \
  -L 127.0.0.1:8189:127.0.0.1:8188 derry
```

Use `scripts/run_comfy_gap_trial.py --help` for one reviewed graph or a sequence
of graphs. A single graph writes to the supplied output directory; multiple
graphs each get a subdirectory. Reusing a completed directory performs no new
submission. A changed graph is refused. New generation still requires the
session's input review; this command is not authorization for new inputs.

The runner uses the existing Pillow environment and standard-library HTTP.
There are no provider calls in this evidence directory or in engine code.
