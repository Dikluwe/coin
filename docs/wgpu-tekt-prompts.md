# SoWgpu experimental rewrite: causal prompts

This document records the prompts that govern the experimental rewrite on
`tekt/sowgpu-render-action`. They are study and traceability artifacts, not a
required source directory layout.

The Open Inventor model is authoritative:

- rendering starts in an `SoAction` and follows its traversal rules;
- scene state continues to use `SoState`, elements, bundles and primitive
  callbacks;
- calculations reuse Coin `Sb*` types and established Coin facilities;
- the WebGPU module remains opt-in and separate from `libCoin`;
- no public Coin 4 ABI change is allowed.

## General prompt

Improve the experimental `SoWgpuRenderAction` as a product while preserving
Open Inventor behavior and Coin conventions. Use Core, Shell, Infra and Wiring
as planning responsibilities, not as mandatory directories or libraries.

**Core** contains Coin-native mechanical transformations needed between scene
capture and the WebGPU connector, and processing of connector results. It may
use Coin types but does not control traversal, submit GPU work or write
user-facing prose.

**Shell** owns language-facing behavior: structured diagnostics, error
messages, textual configuration, logs, frame dumps and profiling output.

**Infra** is the concrete WebGPU connector: private C ABI, Rust bridge, device,
queue, resources, pipelines, surfaces, command submission, readback and device
loss. It does not traverse the Coin scene graph.

**Wiring** orchestrates the Open Inventor integration: action lifecycle,
traversal callbacks, state collection, invocation order, fallback, target
selection and result publication.

For every change:

1. State which responsibilities are affected and which remain unchanged.
2. Preserve the existing public experimental API unless a separate ABI change
   is explicitly approved.
3. Give each semantic decision one owner. Repeat boundary validation only for
   memory, transport and ABI safety.
4. Prefer Coin types and conventions over a parallel representation made only
   to satisfy the planning model.
5. Use a conservative rebuild or fallback when reuse cannot be proven.
6. Keep unrelated changes in `src/base`, `src/threads` and
   `include/Inventor/threads` outside WebGPU commits.
7. Validate Recording and Rust builds. Product claims require Release median,
   p95, visual comparison, memory and phase-separated measurements. Do not
   assume direct RTT or asynchronous readback improves elapsed time.

## Prompt 001: atomize and reuse FFI packing

**Intent:** Separate the C++ `FramePlan` to private C ABI conversion from GPU
submission. Make its ownership explicit and reuse the packed immutable payload
when the same non-zero frame revision is submitted again.

**Core:** unchanged. `FramePlan` and its Coin-native calculations retain their
current meaning.

**Shell:** extend phase diagnostics with a packing-cache hit indicator. Do not
change public error strings except where required to preserve an existing
failure.

**Infra:** introduce a private owner for all arrays referenced by
`CoinWgpuFrameView`. It copies an immutable revision once, updates target
dimensions without repacking, and never exposes the private FFI in public Coin
headers. A zero revision is never reusable. Texture bytes must be owned by the
packed frame so no pointer outlives its source `FramePlan`.

**Wiring:** unchanged. `SoWgpuRenderAction`, traversal callbacks, render target
selection and frame execution order must behave exactly as before.

**Positive oracle:** two submissions of the same non-zero revision produce the
same image and the second reports a packing-cache hit.

**Negative oracle:** a new revision is repacked even if its vector sizes match.

**Unknown oracle:** revision zero is always repacked.

**Gates:** Rust and Recording suites pass; public `libCoin` symbols remain
unchanged; static Release packing time improves without visual or memory
regression.

### Prompt 001 result — 2026-09-24

- Debug/Rust: 38/38 complete tests passed; the 29 WebGPU tests include the new
  positive, negative and zero-revision packing oracles.
- Debug/Recording: 24/24 complete tests passed; the Rust-only packer is not
  part of that target.
- Release/RADV RENOIR, Assembly, 512x512, two warmup and five traced frames:
  cached packing median was 0.000531 ms (`pack_cache_hit=1`), versus the
  previously documented 0.162 ms for the same static phase. This is a 99.7%
  phase reduction; it is not presented as a 99.7% whole-frame improvement.
- Release/RADV RENOIR, Assembly, eight warmup and 30 measured frames: WebGPU
  median/p95 was 1.959/2.327 ms including RGBA readback. The separately run GL
  median/p95 was 0.453/0.851 ms; this GL result differs materially from the
  earlier campaign and is retained as a gate, not combined into a speedup
  claim.
- The isolated WebGPU process reached 140588 KiB peak RSS. The prior campaign
  recorded 148424 KiB, so no memory regression was observed, but the runs are
  not treated as a controlled memory reduction measurement.
- Assembly `BASE_COLOR` retained exact parity after the expected vertical
  flip: RGB MAE 0 and silhouette IoU 1.

## Planned prompts

### Prompt 002: FramePlan algorithms

Move validation, equality and image-result transformations out of orchestration
while retaining Coin-native data types and existing diagnostics.

### Prompt 003: diagnostic Shell

Centralize backend-status translation, structured errors and phase reporting.
Core and Infra identify outcomes; Shell renders them; Wiring chooses fallback.

### Prompt 004: traversal capture versus geometry transformation

Keep `SoCallbackAction`, `SoState`, elements and bundles in Wiring. Separate the
mechanical indexed-geometry and normal algorithms so they can be tested without
driving a traversal.

### Prompt 005: typed frame reuse

Replace binary whole-plan reuse with explicit outcomes: reuse, patch,
resource rebuild, full rebuild or unknown. Start with a camera-only patch and
fall back conservatively when dependencies are not known.

### Prompt 006: owned Rust context

Replace ambient connector state with explicit private context ownership without
changing public Coin ABI. Device, caches, serials, readback and fault injection
must have a defined owner and loss/recovery lifecycle.
