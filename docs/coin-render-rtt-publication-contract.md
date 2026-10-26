# RTT execution and transactional publication (P13)

The common graph/ownership contract is defined in
[RTT ownership](coin-render-rtt-ownership-contract.md). This document preserves
publication rules and identifies the historical P13 RGBA8 profile separately
from later extensions.

SceneTexture NONE forces false texture-transparency classification; ALPHA_BLEND
forces true. Material transparency still participates in MODULATE. Translucent
stored pixels alone do not enable consumer blend under NONE; opaque pixels do
not undo ALPHA_BLEND classification. Wiring captures `sceneTransparencyFunction`;
common composition interprets it once, and builder/equality/reuse/resolution retain
it. CPU/Recording/BGFX/wgpu consume the decision. Physical `gpuOpaque` is separate
from scheduling. Later profiles explicitly define sampling alpha, including NONE's
sampled alpha policy; do not infer current scheduling from stored pixels.

Historical P13 admitted unit **0**, RGBA8/MODULATE, REPEAT/CLAMP, NONE/ALPHA_BLEND/
ALPHA_TEST, nonnull scene without sceneTransparencyType, axes **1..2048**, **8**
levels and **64 MiB/apply** nominal debit. STAGED reads top-down RGBA8 and flips
once into bottom-origin Coin images for consumer upload. DIRECT retains GPU
texture through consumer submission, with connector-owned orientation and domain.
BGFX direct admits prepared OBJECT and qualified peeling/OIT; an opaque frame
need not require peeling merely from declared mode. wgpu's device registry holds
up to **64** textures: preflight checks remaining default-device capacity without
initialization or summing unrelated extra devices. Later profiles extend these
original unit/model/window/mip restrictions; see [resources](coin-render-portable-resources-contract.md).

Common preflight validates dependencies/plans/budget; connector hooks validate
concrete limits before producer work where knowable. Immutable graphs can be
inspected before device initialization. Automatic ordinary-image alpha may wait
for STAGED resolution; SceneTexture's explicit policy does not. Allocation/readback/
runtime/device failures can still occur after GPU work, so admission is not a
universal guarantee of no submissions before every possible failure.

## Atomic publication

`CoinRenderTargetP` owns publication. Backends write candidate buffers. Color,
enabled depth, serial and validated revision publish together only after success
and complete dimensional checks. Failed preflight/prepare/submit preserves prior
image/allocations, including `borrowRGBA()` identity; status/diagnostic report the
failure, while device/resource generations may still advance. Candidate storage
is reused per target without copying the old image for rollback, potentially
holding one extra CPU color/depth pair (**4+4 bytes/pixel** when depth exists).
Resize frees extra storage. This is outside nominal RTT debit.

Async writes a private ticket first. Common checks require valid token, serial,
dimensions, RGBA8 format, pitches and full color/depth byte counts before handing
the descriptor to the caller. Staging pitches may be padded; published vectors
are compact. Core checks sufficient rows/component alignment, connector polling
checks exact native layout. Failure leaves caller descriptor unchanged, including
empty input. A partial allocation returned with error is cancelled and never
replaces a ticket already held by the caller. BGFX checks runtime again before
internal readback publication and returns partial tickets requiring retirement
to the common owner.

Successful async invalidates previous synchronous readback. Tickets own images
independently of action/target until successful poll/cancel. Invalid/pending/lost-
generation polling preserves caller vectors. Current metadata remains under the
ticket contract, distinct from target publication serial.

## Tests and qualification

`CoinRenderPublicationTest` injects partial writes before six failures, SUCCESS
with incomplete buffers and recovery; it checks data/allocation identity, serial
and revision in all three builds. Ownership checks alpha policy independently of
pixels and limits without initializing a GPU. SceneTexture checks orientation,
NONE/ALPHA_BLEND, mutation/dependencies and staged/direct recovery. BGFX async
compares sync pixels and loss after ticket creation, requiring retirement without
runtime-reference leaks. AsyncAction also verifies rejected format preserves
color/depth/serial/borrow and input descriptor, overlapping tickets/lifetime/loss.

Historical Linux/X11/Xvfb campaigns: wgpu **30/30 (52.71 s)**, broad BGFX **70/70
(199.27 s)**, final affected BGFX **16/16 (45.19 s)**, descriptor/padding checks
**3/3 (3.31 s)**, CPU **4/4 (0.49 s)**, Rust **16+2**. An initial real wgpu pitch
failure exposed the manager's 16×16 staging alignment to **256 bytes**; Core was
corrected and regression-tested before the full rerun. It was not dismissed as
an EGL environment warning. Final campaigns had no skips. Full receipts/pilots
remain [archived](coin-render-evidence.md).

ALPHA_TEST SceneTexture follows the qualified native policy: transparent
classification/composition/caster exclusion like ALPHA_BLEND without automatic
discard. This is distinct from an explicit SoAlphaTest fragment function. Shared
recovery/admission is in [multiple targets](coin-render-multi-target-contract.md),
explicit window capture in [window readback](coin-render-window-readback.md).
Expanded formats/states, driver-total memory and the physical platform matrix
retain separate qualification; P13 closure never certified all F14/F15 behavior.
