# Rust/wgpu material-resource reuse

The qualified opaque-instancing profile retains immutable GPU buffers per device.
Geometry still needs exact equality or identity of an owned Rust snapshot. A
revision, element count or caller pointer does not replace that proof.

After geometry qualifies, compare all instance/material bytes independently
against the last successful submission snapshot. Table changes may retain
instances; model changes may retain materials. Material indices, normal matrices,
signed zero, lengths or any material field invalidate the respective buffer.
Without qualifying geometry, retain conservative behavior. Buffers remain
immutable on the same device; a miss creates new resources rather than changing
ones an earlier submission may use. Publish snapshots and instanced resources
only after success. Validation, limits, failure/retry, device destruction and
null-camera-payload rules remain unchanged.

Pack the GPU table only when creating its buffer, including frame/camera cache
hits. `CoinWgpuMaterial` remains **72 bytes**, `GpuMaterial` **80 bytes**, with
two trailing zero floats. Clear without materials uses the 80-byte sentinel.
This optimization changes neither private ABI nor shaders.
`COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE=1` restores eager packing at the
original point and coupled invalidation for same-binary comparison. The animation
runner clears controls with prefix `COIN_WGPU_DISABLE_`.

Phase-trace events describe attempts, not final submission success:

- `rust_material_resources`: counts, source/GPU/pack bytes, comparison/pack times,
  independent hits, buffer creation/upload bytes. `resource_ms` includes creation/
  reuse of both buffers, not GPU duration. Pack excludes later temporary release.
- `rust_material_snapshot`: copied material/instance bytes; `snapshot_ms` covers
  the entire snapshot, including geometry/state/order, not isolated table copy.
  Camera snapshots share owned geometry and report zero for those copies.
- `rust_instances`: retains prior fields and adds separate hits/uploads;
  `payload_hit` still means both buffers hit.

These scopes nest inside existing phases; do not sum them. CPU rebuild snapshots
may still copy materials/instances when a GPU buffer is reused. This is not
partial-table upload or elimination of mandatory snapshot copies. BGFX embeds
materials in each **160-byte** C++ instance; this separate Rust/GPU table policy
does not apply to that representation.
