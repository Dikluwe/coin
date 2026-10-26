# RTT planning and ownership (P12)

`SoSceneTexture2` captures a producer scene and applies its result to later
geometry. Background, dimensions, sampler and Coin revision are captured.
Quality zero/image override suppress producer capture and its budget charge.
Native CoinGL uses FBO/SoGLImage or pbuffer/readback tied to a cache context,
including scheduled destruction. The contract is matching live context, producer
and result; a concrete GL token/framebuffer/context is not portable semantics.

## Common graph and executor boundary

Wiring reads nodes/state and `getNodeId()`, uses child actions for capture and
publishes status. It does not prepare backends or create/retire GPU tokens.
`CoinRenderRttPlan` owns topological order, identity, deduplication, dimensions,
budget and invalid-reference/cycle rejection. `CoinRenderRttResources` checks
owner/device/generations and resolves descriptions. Neither Core traverses Coin
nor calls GPU. `CoinRenderRttExecution` executes passes and retains results until
consumer submission; BGFX/wgpu implement domain/direct/retirement hooks. Shell
owns textual defaults; typed options propagate without reinterpretation.

`producerId` is a logical reference starting at **1**, valid only within one apply
graph. Zero means ordinary image. It is never a node pointer/GPU token. Producers
have size, Coin source revision and their own frame plan; texture dependencies
point only to earlier producers. Equal source/revision/dimensions/payload may
share a producer. Distinct origins have distinct logical IDs even with equal
pixels; changed revision/payload requires another producer. Physical content
reuse can be executor-specific without changing identity.

Capture keeps `gpuToken=0`. Execution resolves a copy to staged pixels or opaque
connector handles. Core does not interpret token values. FFI/CPU/BGFX reject
unresolved logical references. Captured/original plans remain immutable.

## Domains and lifetime

Each target gets a process-monotonic owner independent of address; targetless
Recording gets an owner per scope. Retained results stamp logical ID, producer
revision, owner, target generation, device domain and device generation.
Resize advances resource generation separately from public ticket generation;
device loss invalidates resources. BGFX identifies its shared runtime with a new
generation per initialization; direct resources belong to the target backend.
C++ wgpu uses bridge default device: a noninitializing epoch query returns zero
for unavailable/lost. Extra private FFI devices do not become publicly selectable.
Staged retained results are CPU bytes with GPU domain zero, independent of the
producer's later device lifetime.

Resolution rejects wrong owners/devices, resize/generation changes or missing
results and preserves caller output. Scope exit invalidates the table. Execution
retains even partial tokens returned with error and retires every exit path after
consumer submission/abandonment. wgpu retirement follows queue serial and tolerates
discarded generations; BGFX marks used resources reusable and drops unused ones.
Backend reconstruction invalidates concrete cache; applying a scene without RTT
also ends the prior BGFX resource set. Async GPU submission retains required
resources until completion, without transferring action/plan pointers to callbacks.

Action capture cache is conservatively disabled for RTT graphs, staged or direct;
logical apply IDs never cross to another apply. Backend texture/geometry caches
remain possible. The last staged frame may retain CPU pixels for accurate Recording
alpha logs; the action's last plan retains no GPU token.

## Admission, budget and historical scope

Capture the entire producer graph before executing its first producer, including
STAGED. Core preflight validates graph, dimensions, budget and profile plans.
Known capture/profile failures publish no parent and execute no producer. Automatic
ordinary-image alpha can require staged pixels before mechanism selection;
SceneTexture uses explicit Coin policy instead. P13 corrected P12's insufficient
pixel-only inference: NONE ignores texture alpha for scheduling, ALPHA_BLEND forces
it. See [publication](coin-render-rtt-publication-contract.md).

The initial profile was unit **0**, RGBA8/MODULATE, REPEAT/CLAMP, NONE, no
sceneTransparencyType, dimensions **1..2048**, depth **8**, nominal **64 MiB/apply**.
Charge STAGED **4 bytes/pixel per occurrence**; DIRECT **8 bytes/pixel per distinct
producer**. This is a contractual debit, not peak CPU/GPU/driver memory. Later
profiles add units/models/explicit policies, direct-window consumers, staged mips
and other resource families; see [portable resources](coin-render-portable-resources-contract.md)
and [scope matrix](coin-render-feature-matrix.md). Historical rejection of every
window/direct or quality above 0.5 is not a universal current restriction.
Each producer starts identity texture matrices, sets its own and restores consumer
matrices. Original FBO/pbuffer filtering/orientation evidence remains archived.
External viewports are converted per producer by Core; not all intersections with
formats/transparency/shadows are qualified. BGFX staged execution now keeps the
consumer prepared while children run, superseding the old runtime-release policy.

`CoinRenderRttOwnershipTest` covers logical identity/revision/payload/origin,
topology/cycles/eight levels, dimensions/64 MiB, owners/devices/generations,
immutable capture, preserved-output resolution and retention/partial cleanup.
GPU controls distinguish same-origin different payloads without reusing an in-use
framebuffer. SceneTexture/Budget/MultiTarget tests cover orientation, mutation,
nesting, shared producers, resize/loss and recovery. Historical P12 campaigns:
wgpu **29/29**, BGFX **67/67** plus **2** explicit ownership cases, CPU **3/3**,
Rust **15+2**. Initial EGL/Xvfb failures and later successful reruns are retained,
not driver stability certification. Original receipts and later qualifications
are in the [archive](coin-render-evidence.md); these counts are not current HEAD runs.
