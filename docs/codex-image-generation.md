# Subscription-backed Codex image generation

**Status:** Feasibility, process transport, typed protocol, provider adapter,
service composition, invariant hardening, and runtime provider selection are
implemented. The final live editor smoke test remains.

## Goal

Run one small, headless `codex app-server --stdio` child beside Zebes and use
the active ChatGPT subscription for prop-artwork image generation. This path
must not require or forward `OPENAI_API_KEY`, must preserve the existing
provider-neutral `ImageGenerationEngine` boundary, and must not expose Codex,
JSON, or generated staging files to editor models.

The API-backed `OpenAiImageClient` remains available as an explicit UI
alternative and for its opt-in live integration test. The two adapters
converge at `ImageGenerationClient`; `EditorUi` owns both services while the
Prop Artwork editor receives only provider-neutral names, availability, and
engine borrows.

## Architecture

```text
EditorUi
  -> ImageGenerationService
    -> ImageGenerationEngine
      -> CodexImageClient
        -> CodexAppServerProtocol
        -> CodexAppServerTransport
          -> codex app-server --stdio
```

- `CodexAppServerTransport` owns the child and its private working directory.
  Its created, running, and stopped states are mutually exclusive; only the
  running state owns the PID, process group, and move-only pipe descriptors.
- `CodexAppServerProtocol` is the only production JSON boundary. It owns
  request correlation and translates JSONL into exhaustive typed events.
- `CodexImageClient` uniquely owns one lazy session and multiplexes up to the
  engine's existing eight-operation bound over that child. Individual request
  objects borrow the session and are destroyed before their client by the
  engine's member order.
- Successful files are accepted only when they are absolute, regular,
  non-symlink files inside either the private working directory or the active
  Codex home's `generated_images` cache and within configured byte and
  decoded-pixel limits. Zebes removes files it owns in the private directory
  after decoding, but leaves Codex-owned cache files to Codex.

## Security and failure policy

- The child environment explicitly removes `OPENAI_API_KEY`.
- The session accepts only an active `chatgpt` account and discovers the
  enabled `imagegen` skill before starting work.
- Threads are ephemeral, use `approvalPolicy: never`, use the
  `workspace-write` sandbox, and run in a newly created private directory.
- The App Server's image tool writes its result to the Codex-managed image
  cache even when the thread `cwd` is private. That cache is the only allowed
  external output root; arbitrary paths remain permission failures.
- Any approval or dynamic-tool request is rejected and permanently fails the
  session.
- Any malformed protocol message, unknown response ID, ambiguous protocol
  write, or invalid state transition permanently fails the session and every
  sibling operation. Zebes does not continue when it cannot know what reached
  the child.
- Cancellation removes local ownership immediately. If a turn exists it first
  queues `turn/interrupt`; a failed interrupt write poisons the shared session.
- Subscription usage limits surface as `ResourceExhausted`.

## Milestones

### 0. Feasibility gate — complete

`scripts/codex_imagegen_probe.py` exercises the App Server handshake, ChatGPT
account check, skill discovery, ephemeral thread and turn, image completion,
and confined output without using an API key. Its deterministic process and
protocol behavior is covered by `tests/codex_imagegen_probe_test.py`.

### 1. Production process and protocol boundaries — complete

- Non-blocking JSONL stdio transport with bounded writes, protocol lines, and
  diagnostics.
- Process-group shutdown and private-directory cleanup.
- Typed request/response translation with JSON and JSON exceptions isolated to
  one `.cc` file.
- Session and operation failures are distinct models; no zero operation-ID
  sentinel is used.

### 2. Provider adapter and hardening — complete

- Lazy ChatGPT-authenticated session and `imagegen` skill validation.
- Multiplexed generation, cancellation, timeouts, usage-limit mapping, output
  confinement, decoding, and cleanup.
- Variant-based session, operation, transport, and protocol outcomes prevent
  contradictory states. Protocol dispatch is compile-time exhaustive.
- `ImageGenerationService::CreateCodex` composes the adapter into the existing
  engine and runner stack.

### 3. Editor provider selection — implemented; live completion smoke remains

`EditorUi` composes Codex and OpenAI independently and offers both in the
Generate source controls, with Codex selected first when it can be constructed.
Failure to construct either stack is an unavailable provider, not an editor
startup failure. Codex executable discovery checks `ZEBES_CODEX_BIN`, `PATH`,
and known macOS OpenAI editor-extension locations during provider composition,
so a missing executable starts disabled with an actionable reason.
Authentication and skill checks remain lazy; a configuration failure from the
first request disables that provider and leaves the user free to select the
other one.

The Generate source controls provide large multiline editors for the subject
prompt and editable system instructions. The default instructions ask for one
uncropped, isolated subject without scenery or other background content,
preferring transparency when the provider supports it and otherwise requesting
a contrasting flat background for deterministic isolation. Codex places them
in the ephemeral thread's developer instructions; the Images API has no
equivalent role, so its adapter composes them ahead of the subject request.
Stable provenance retains the user's subject prompt.

Art direction is a separate editor draft rather than part of those isolation
requirements. The UI offers Custom, Retro exploration, Modern pixel art,
Hand-painted platformer, Dark biomechanical, Stylized fantasy, and Clean
cartoon presets. Selecting a preset replaces the visible, editable Style
guidance text; editing that text switches the selection to Custom. Non-empty
guidance is appended under an `Art direction:` heading when the provider
request is composed, so changing style never overwrites the background and
composition constraints.

The broader Prop Artwork tab remains enabled when neither provider works,
because importing and processing retained PNG sources is an offline workflow.
Only the Generate source section is disabled, with the provider failure shown
in place.

Acceptance:

- The editor starts without `OPENAI_API_KEY` when Codex is selected.
- The child remains lazy until the first generation request.
- Failure to find Codex disables it at startup. A missing ChatGPT login or
  enabled skill is reported by the first request and then disables Codex.
  Neither prevents unrelated editor use or selection of OpenAI.
- Live startup, ChatGPT authentication, skill discovery, and generation are
  confirmed. The first editor run exposed the App Server's Codex-cache output
  path; the allowlist fix has focused coverage. Remaining live check: decode
  the result, review, accept, discard, cancel, and shut down through the real
  editor flow.

### 4. Operational and platform follow-up

- Exercise editor shutdown during a real in-flight Codex turn.
- Decide whether retry is safe for any pre-turn failures; never retry an
  ambiguous write or generation turn automatically.
- Add a Windows process transport before advertising this provider there. The
  current factory returns `Unimplemented` on Windows.
- Re-run the feasibility probe after Codex App Server upgrades that change the
  observed protocol, account model, or image-generation item schema.

## Verification

The deterministic boundary is covered by:

```bash
python3 -m unittest tests/codex_imagegen_probe_test.py
scripts/test.sh codex_app_server_protocol_test
scripts/test.sh codex_app_server_transport_test
scripts/test.sh codex_image_client_test
scripts/test.sh image_generation_service_test
```

The probe or final editor smoke test can consume subscription image-generation
usage. Do not put either live action in CTest or CI.
