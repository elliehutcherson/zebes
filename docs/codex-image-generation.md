# Subscription-backed Codex image generation

**Status:** Feasibility, process transport, typed protocol, provider adapter,
service composition, and invariant hardening are implemented. The production
editor still selects the API-backed OpenAI adapter; runtime provider selection
and the final editor smoke test remain.

## Goal

Run one small, headless `codex app-server --stdio` child beside Zebes and use
the active ChatGPT subscription for prop-artwork image generation. This path
must not require or forward `OPENAI_API_KEY`, must preserve the existing
provider-neutral `ImageGenerationEngine` boundary, and must not expose Codex,
JSON, or generated staging files to editor models.

The API-backed `OpenAiImageClient` remains available as an explicit alternative
and for its opt-in live integration test. The two adapters converge at
`ImageGenerationClient`; provider selection belongs at the editor composition
root rather than in the prop-artwork feature.

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
  non-symlink files inside the private working directory and within configured
  byte and decoded-pixel limits. Zebes decodes them to `RgbaImage` and removes
  the staging file before returning provider-neutral data.

## Security and failure policy

- The child environment explicitly removes `OPENAI_API_KEY`.
- The session accepts only an active `chatgpt` account and discovers the
  enabled `imagegen` skill before starting work.
- Threads are ephemeral, use `approvalPolicy: never`, use the
  `workspace-write` sandbox, and run in a newly created private directory.
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

### 3. Editor provider selection — next

The current editor composition root still calls
`ImageGenerationService::CreateOpenAi`. Add an explicit startup provider
selection and make Codex the normal local-editor path while retaining OpenAI as
an opt-in alternative. Keep provider details out of `PropArtworkEditor`; it
continues to receive only `ImageGenerationEngine&`.

Acceptance:

- The editor starts without `OPENAI_API_KEY` when Codex is selected.
- The child remains lazy until the first generation request.
- Generate, cancel, review, accept, discard, and shutdown work through the real
  editor flow.
- Failure to find Codex, a ChatGPT login, or the enabled skill is reported as a
  generation failure without preventing unrelated editor use.

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
