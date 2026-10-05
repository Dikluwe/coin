# Geometry overlay collision validation

`CoinRenderFrameReuseCore::beginObjectOverlay` validates positions and prepares
their undo before publishing any model, material, vertex or draw-source change.
Slot bounds, finite coordinates, the inclusive coordinate domain and the
existing update-count limit apply to every input entry.

Uniqueness validation uses the current input only:

- Strictly increasing slots prove uniqueness without collision scratch or sort.
- Otherwise, maximal consecutive runs are collected as inclusive `[first,last]`
  intervals. Only `slot == uint64_t(last) + 1` extends a run. Sorting these
  intervals and rejecting `next.first <= previous.last` detects all repeated
  slots, including identical repeated values and partially overlapping runs.
- Optional scratch starts on the first inversion, reconstructing the already
  validated prefix. Its capacity is at most 65,536 intervals and at most the
  original `N * sizeof(uint32_t)` slot reservation. Exceeding this capacity or
  failing the optional allocation releases the interval scratch and uses the
  literal slot-vector sort and adjacent-duplicate check.

The optimization recognizes slot order, without assumptions about node type or
scene identity. Updates and undo retain their original input order. No metadata
survives the call, and collision rejection leaves both the plan and caller undo
unchanged. Mandatory undo and literal-fallback allocation errors retain their
existing exception behavior.

`COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION=1` forces the literal collision
validator for same-binary comparisons. The benchmark runner clears this optout
when preparing its environment.

With phase tracing enabled, a successfully validated nonempty position list emits
one `geometry_overlay_validation` event before the remaining draw/model checks.
An event therefore proves position validation, not final submission success.
Its fields are:

- `positions`: input entries.
- `runs`: observed maximal consecutive runs; zero in literal mode.
- `mode`: `ordered`, `intervals` or `literal`.
- `sorted_items`: zero, interval count or position count, respectively.
- `scratch_bytes`: capacity of collision scratch used during validation,
  excluding undo, allocator overhead and resident memory.
- `validation_ms`: position undo reservation, bounds/numeric validation, scratch
  construction, uniqueness checks and scratch cleanup. It excludes trace output,
  later draw/model checks, mutation and backend execution.

No event is emitted for empty or rejected position lists. Timers are active only
when tracing is enabled. Per-frame analysis must correlate the event with the
following action marker before removing warmups; phase timings may overlap other
measurements and cannot be added indiscriminately.
