# Options, mechanisms and capabilities (P11)

CoinRender separates hardware facts, implemented mechanisms and qualification
evidence. `CoinRenderSelectionCore.h` owns pure policy. Wiring captures choices;
Infra supplies facts/executes; Shell parses textual defaults.

```cpp
CoinRenderOptions options;
options.renderer = COIN_RENDER_RENDERER_VULKAN;
options.transparency = COIN_RENDER_TRANSPARENCY_COIN;
options.sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
auto * target = CoinRenderTarget::createOffscreen(SbVec2i32(640, 480), options);
```

Target factories accept options and expose `getOptions()`; choices are immutable
for target lifetime. Factories without explicit options read defaults at creation:
`COIN_BGFX_RENDERER`, `COIN_BGFX_TRANSPARENCY`, `COIN_RENDER_RTT_GPU_DIRECT`
(and legacy wgpu RTT alias). BGFX renderer/transparency strings apply only to
BGFX builds. Invalid defaults reject action entry before traversal/subpasses;
the action does not parse environment strings. Explicit options ignore these
defaults, including invalid strings. Changing environment cannot reconfigure an
existing target: create another and detach the action before destroying the old
one. Profiling, failure injection, cache and readback controls remain Infra-specific.

UNKNOWN renderer uses connector default. BGFX selects Vulkan/OpenGL; an explicit
wgpu renderer must match its active adapter, without switching shared runtime or
falling back.

| Transparency option | Rule |
| --- | --- |
| COIN | Preserve captured Coin modality; do not choose OIT from draw count. |
| OBJECT | Object modalities; reject SORTED_LAYERS_BLEND. |
| PEELING | Explicit layered deferred base-layer source-over extension. |
| WEIGHTED_OIT | Explicit BGFX/wgpu approximation for eligible draws; CPU rejects when required. |

Immediate/additive/annotation passes keep their common contract. Opaque frames
need not allocate extension attachments. Peeling retains its own limits/budget.
Options participate in equality/reuse and propagate to staged/direct children;
BGFX direct uses private peeling/OIT attachments in qualified profiles.

## Capability V3 semantics

`coin_render_query_capabilities` accepts exact V1/V2 sizes and writes only the
corresponding prefix. V3 adds:

- `known_hardware_facts`/`known_formats` distinguish unknown from absent; renderer,
  IDs, formats, limits and features come from native probe. wgpu does not know
  physical D24S8: Depth24PlusStencil8 is not proof. Zero means absent only with a
  matching knowledge bit.
- `implemented_mechanisms` describes connector implementation, independent of
  adapter: CPU objects/peeling; GPU connectors also weighted OIT.
- `available_mechanisms` describes execution after probe. wgpu enabled-device
  features differ from physical-adapter possibilities.
- `qualified_profile_mechanisms`/`qualified_profiles` describe bounded P08/P09/P10
  evidence per connector/renderer/offscreen route; they do not certify new devices,
  drivers, arbitrary state or real windows. Window query masks remain zero because
  adapter probe does not present a window.
- `max_peel_layers` is implemented limit **8**, separate from availability.

Legacy `features` describes compiled profile, not hardware/qualification. Maximum
texture units are **8** for wgpu/CPU too. Procedural-coordinate feature bit describes
its initial P07 profile, not every generator/device. `gpu_available` proves neither
OIT nor depth overrides/particular RTT states. BGFX probe ignores transparency
mode to measure hardware; invalid mode/extension requirements do not hide an
adapter. A runtime busy with another renderer/thread returns BUSY. Explicit
`coin_render_query_capabilities_for_renderer` bypasses defaults. Probe may initialize
runtime/allocate resources; it does not prove physical presentation.

`coin_render_select_mechanism(caps, mechanism, require_qualified_profile)` is pure
and returns mechanism, reason and profile evidence. Reasons distinguish invalid
request, missing implementation, hardware absence, pending probe and unqualified
profile. Qualification is optional and never inferred from hardware. Captured
modality and OBJECT/layers conflicts use the same Core. Unsupported requests
preserve image/serial and permit later valid input, without substitution. Selection
enums do not subsume traversal/resource/device-loss statuses; observe target/apply.

Historical tests in `CoinRenderSelectionTest` cover synthetic facts, precedence,
BUSY, versions/write boundaries, concrete probe, window qualification absence,
explicit options vs environment, RGB/conflicts/recovery. Original 2026-09-28 closure:
wgpu **28/28 CTests**, BGFX **58** distinct passes after fixing two OBJECT fixture
requests, Rust **15 unit + 2 shader**, public C header compilation; no final CTest
skips. Later fixes propagate options into targetless RTT planning children and
count nested direct attachments. External viewport handling is now Core-owned.
Those receipts remain [archived](coin-render-evidence.md); other platform/driver/
RTT-state qualification remains separate.
