# CoinGL compatibility and bug handling

Policy established on 2026-10-06 and refined on 2026-10-12: CoinGL is the
historical compatibility reference for CoinRender, but does not alone determine
correctness. Do not reproduce confirmed bugs, invalid accesses or undefined
behavior merely to match a legacy image. Preserve valid scene semantics in
BGFX and wgpu; CPU is an auxiliary test executor.

## Determining the expected result

1. Specify the input and documented Coin node/state semantics. Fix relevant
   units, coordinate spaces, projection, composition order, formats, color space
   and precision.
2. Apply the mathematics of the chosen visual model and graphics API contracts.
   Changing executor does not turn legacy approximate lighting into PBR; a new
   model requires its own contract and mode. Where specifications allow multiple
   results, state the portable policy before comparing pixels.
3. Compute an independent expectation for the tested property: transforms,
   coverage, depth, interpolation, texels, blending or light contribution. The
   control must detect an error shared by CoinGL and CoinRender.
4. Compare CoinGL, BGFX and wgpu in the same environment and classify differences.
   CoinGL establishes compatibility in the valid domain; it does not replace
   the preceding reasoning.

API/driver conversions (texture origin, depth interval and window presentation)
belong to executors. Observable results still obey the common contract. If a
deliberate portable rule differs from CoinGL, record both results and the reason;
do not call that exact visual parity.

## Deterministic delivery and native numeric results

The 2026-10-18 B22 decision guarantees correct preparation and delivery of scene
semantics to the GPU: defined state, geometry, attributes, uniforms, formats and
operation order. Equal input/configuration must yield equivalent delivery,
independent of residual state or undocumented cache history. Resource IDs and
addresses need not match.

Pixel identity across GPUs, drivers or APIs is not a general acceptance criterion
for the native path. Small characterized rasterization/interpolation/precision
differences with correct inputs and no evidence of an API violation are accepted
native results. Do not alter geometry, comparators or product precision solely
to match machines. Explicit canonical profiles retain their own contracts.

Visual size alone does not classify a difference. Wrong inputs, missing state,
invalid accesses, undefined API use, publication failures and lost geometric
coherence remain bugs. Undiagnosed cases require the neighborhood investigation
below. B22 is closed as accepted native numeric variation only in its investigated
32F domain, without a renderer fix. Its strict differences remain diagnostic
facts, not identical images or retroactively passing historical comparisons.
Other differences do not inherit this classification. The complete B22 study is
in the [companion archive](coin-render-evidence.md) at its original dated path.

In precise `SoQuadMesh` mode, the geometric center averages four vertices, while
attributes use legacy weights quantized by the binary exponent of squared-distance
ratios. Builds without `ilogb`/`_logb` use the legacy fixed-zero exponent fallback.
For ratio one, weights are approximately `(0.23205, 0.23205, 0.26795, 0.26795)`,
not four quarters. Original GL and portable capture preserve this rule. A texture/
LOD oracle must account for the fan triangle and its derivatives; assuming affine
interpolation at this center is incorrect. Avoid triangle edges for numeric
witnesses, where derivatives can differ. This does not classify other differences.

## Undocumented behavior and neighborhood investigation

Policy dated 2026-10-18: if documentation leaves the result unspecified, first
state a plausible CAD expectation consistent with geometry, mathematics and
defined API rules. Occlusion should respect depth, and a continuous surface
should not acquire holes without geometric/coverage cause. This is an explicit
contract hypothesis, not proof of historical Coin intent.

Before classifying an edge or threshold anomaly:

1. Compare adjacent pixels, primitives and samples: interior, contour and both
   sides of the threshold. Change one input at a time with small camera/geometry,
   depth, UV, alpha or resolution perturbations; record actual values and extent.
2. Check expected discontinuities. Raster silhouettes, quantization, alpha tests
   and exact comparators need not vary smoothly. Perturbations diagnose behavior;
   they are not implementation epsilons.
3. Make controlled software changes: disable a suspected cache, rebuild state,
   specify an inherited binding or use an equivalent operation. Require equivalent
   output for semantics-preserving changes. State the expected effect in advance
   when deliberately changing the tested hypothesis.
4. Run original and variant on the same input/neighborhood with an unaffected
   control. Check persistence, disappearance, displacement and restoration.
   Persistence alone does not prove correctness; disappearance alone does not
   prove cause.
5. Combine the plausible contract, graphics rule and causal evidence. Bound
   legitimate differences. Fix confirmed bugs and add regressions for the case
   and its neighborhood. Keep an undiscriminated cause open if evidence cannot
   distinguish hypotheses.

Do not turn an isolated legacy pixel into a contract, or move the input, reference
or tolerance merely to pass a comparison. Diagnostic modifications are temporary;
only justified fixes and relevant regressions belong in the product.

## Classifying differences and evidence

| Classification | Required treatment |
| --- | --- |
| Valid Coin behavior | Preserve it and compare qualified scenes/platforms with CoinGL. Unexpected appearance alone is not proof of a bug. |
| Confirmed bug/undefined behavior | Define and test correct safe behavior; record deliberate divergence. Defective CoinGL output is not the expected value. |
| Accepted native numeric variation | Demonstrate correct state/input and characterize the difference without contract violation; retain cross-machine comparison as diagnosis. |
| Undiagnosed difference | Keep a minimal scene and evidence; claim neither parity nor a bug exception. |
| API/driver adaptation | Identify the conversion and verify observable semantics. |
| Unsupported feature | Record the gap and diagnostic; missing support is not a CoinGL bug fix. |

An exception identifies a minimal scene, Coin revision/environment, demonstrated
cause and expected semantics. Use Coin documentation, relevant specifications and
independent calculations/tests where applicable. Driver, raster precision and
tolerance differences need their own diagnosis. A regression must detect the
fault and verify the correction, including state restoration. Invalid inputs may
be explicitly rejected while preserving the previous valid frame, or receive a
defined safe treatment. Increasing tolerance, dropping discrepant pixels or
removing the revealing comparison is insufficient.

Separate proven parity, deliberate bug divergence, open differences and support
gaps. One exception does not certify a whole feature. Performance and ABI
compatibility have separate qualifications. The [marker contract](coin-render-marker-contract.md)
illustrates safe handling of short lists/missing bitmap IDs where Release CoinGL
can access out of bounds; those cases are distinct from valid-bitmap comparisons.
