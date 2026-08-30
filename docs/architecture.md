# Zebes architecture

Current dependency and ownership index. Read only the domain section relevant to
the task. The prior cross-system narrative is preserved in
[`history/architecture-through-2026-08-30.md`](history/architecture-through-2026-08-30.md).

## Dependency direction

```text
objects / common
      ↓
terrain / engine / artwork
      ↓
resources / generation / curation
      ↓
api
      ↓
editor / game / scripts
      ↓
platform adapters
```

Domain and engine interfaces use Zebes-owned types. SDL, ImGui, HTTP, filesystem
protocol details, and provider JSON remain behind adapters or composition roots.
A lower layer never imports an editor, game host, or native renderer to make one
caller convenient.

## Domain references

- [`architecture/runtime.md`](architecture/runtime.md): `GameRuntime`,
  `RuntimeWorld`, loaded-level ownership, fixed ticks, animation state, M4
  snapshot/thread boundaries.
- [`architecture/resources-and-data.md`](architecture/resources-and-data.md):
  strict definitions, managers, `TextureHandle`, asset workspaces, recipes,
  retained source, migrations, and transaction rules.
- [`architecture/editor-and-authoring.md`](architecture/editor-and-authoring.md):
  editor model/panel separation, drafts, save/revert, generated/imported
  authoring, background work, and cross-editor stable IDs.
- [`architecture/rendering-and-platform.md`](architecture/rendering-and-platform.md):
  scene composition, camera transforms, renderer/resource boundaries, SDL
  ownership, input, and backend rules.
- [`architecture/terrain-and-levels.md`](architecture/terrain-and-levels.md):
  one-tileset levels, sparse layers, terrain autotiling, collision geometry,
  parallax zones/themes, and level review.

## Global invariants

- Serialized definitions under `src/objects/` contain no mutable runtime state.
- Every definition field is required. Readers use strict access; writers emit
  complete objects. Format changes require migration and shipped-definition
  tests.
- Resource managers own definitions and native resource lifetimes. Callers keep
  stable IDs or copied snapshots, not pointers across catalogue refreshes.
- `TextureHandle` is opaque outside the owning resource store. Never construct
  or reinterpret native handles manually.
- Runtime and editor scene composition are platform-neutral. Native renderers
  resolve handles and issue draw calls.
- Editor drafts do not mutate managed resources before explicit save/commit.
- Background workers operate on copied immutable inputs and return values or
  events; manager and GPU mutation returns to the owning thread.
- Creation/regeneration preflights all outputs and references, checks optimistic
  snapshots, commits in dependency order, and compensates in reverse order.
- Deletion scans external references and removes only complete owned graphs.
- Errors stop the whole operation or frame rather than silently substituting
  placeholders or partial state.

## Composition roots

- `AssetWorkspace` owns manager construction, loading profiles, and API-visible
  catalogues for editor, headless tools, and runtime boot.
- `EditorEngine` owns the interactive editor services and native editor host.
- `GameRuntime` composes frozen loaded content, mutable runtime simulation, scene
  publication, and the SDL game host.
- Headless tools replace native texture resources with
  `HeadlessTextureStore` but use the same managers and `Api` contracts.

## Verification boundaries

- Pure domain/geometry/processing code receives copied values and has
  platform-neutral tests.
- Managers test strict serialization, reference scanning, and lifecycle rules.
- API tests cover preflight, stale snapshots, commit ordering, and compensation.
- Renderer/store tests may mock native wrappers but do not leak native types
  upward.
- UI tests verify intent routing and visible contracts; live gates decide visual
  quality that automated tests cannot establish.

## Active plans

- [`roadmap.md`](roadmap.md): current sequencing.
- [`handoff.md`](handoff.md): concise resume point.
- [`engine-runtime-plan.md`](engine-runtime-plan.md): runtime M4/M5.
- [`environment-artwork-plan.md`](environment-artwork-plan.md): remaining
  environment/content contracts.
- [`animation-artwork-pipeline.md`](animation-artwork-pipeline.md): imported/
  manual production frame-set pipeline.
- [`prop-artwork.md`](prop-artwork.md): prop lifecycle and remaining provider
  follow-up.
