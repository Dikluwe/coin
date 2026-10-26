# Evidence and archived development documents

The submission keeps English maintainer guides and detailed current contracts.
Dated campaigns, duplicated studies, development plans and validation artifacts
are archived outside the tracked submission tree. They were archived, not all
translated. Upstream documentation present at the common base is unchanged.
[The archive manifest](coin-render-archived-documents.json) lists removed original
paths. It includes old names and local integration reports for traceability;
those names are historical context rather than current product terminology.

A [local companion archive](../coinrender-evidence.tar.gz) is supplied beside
this checkout. It is not yet remotely published. Its `integrated/docs/` and
`historical/docs/` prefixes preserve original document paths and receipts.
For example, a former `docs/validation/<campaign>/summary.md` link can be located
under the corresponding prefix in the archive. Inspect its contents before
selecting an extraction path:

```sh
tar -tzf coinrender-evidence.tar.gz
mkdir -p /tmp/coinrender-evidence
tar -xzf coinrender-evidence.tar.gz -C /tmp/coinrender-evidence
```

Complete original source/evidence snapshots are also preserved locally under
`/mnt/Laranja/Git/externos/coin/build-coinrender-consolidation-20261025/` as
`integrated-source-and-evidence.tar.gz` and
`historical-source-and-evidence.tar.gz`. These are local preservation paths,
not published upstream locations. The local companion must accompany a review
until receipts are published at a durable location and the links are updated.
Do not present a developer-machine absolute link as remotely accessible evidence.

Historical results retain their original date, revision, exact profile, GPU,
API, driver, surface, reference availability and limitations. They do not imply
a rerun at the current contribution HEAD. A feature mask is an implementation
claim; available hardware and qualified evidence are separate facts. A skipped
case or a pure capture/CPU result does not qualify real GPU presentation.

B34 external validation, B94 and B95 remain open. Windows DX12/OpenGL/Vulkan
are not newly qualified by this documentation consolidation. Local host patches
and integration macros are excluded from the generic source submission; synthetic
Coin tests and exported scene graphs do not certify those host applications.

Historical browser WebGPU results require particular care: the TCL 40,000 case
recorded a median 8.50 FPS over 120 measured frames after 10 warmup frames,
with 17.02 s of preparation; the million-object case took 365.68 s to prepare with a
clear image and is **not qualified**. G7 million has no valid FPS. A clear image,
a completed timer or an unvalidated throughput log is not a successful render.
No performance ratio may be inferred from these cases. Compare only validated
images, matching scenes, timings, warmup policies, dimensions and environment.

Contract documents omit most dated run-by-run narration while preserving
semantic rules, admitted domains, ABI/layout caveats, rejection/publication
behavior and the qualifications needed to interpret prior receipts. For the
original full narration and numeric logs, use the matching original document in
`integrated/docs/` or `historical/docs/`.
