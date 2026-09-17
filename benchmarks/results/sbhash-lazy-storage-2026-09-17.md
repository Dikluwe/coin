# SbHash lazy-storage benchmark — 2026-09-17

## Compared revisions

- Eager-storage baseline (PR 5): `8eab4ecf7ee1`
- Lazy-storage experiment (branch 6): `c6b1e73963b0`
- Compiler: GCC 13.3.0
- CPU: AMD Ryzen 7 5800H
- Configuration: `Release`, `-O3 -DNDEBUG`, C++11

Both executables were compiled from the same benchmark source. Only the
`SbHash.h` include path differed. Runs were pinned to CPU 2. Three 20-sample,
50 ms target-duration rounds were executed in alternating order:
baseline/experiment, experiment/baseline, baseline/experiment.

## Timing summary

Values are the arithmetic mean of the median nanoseconds per item from the
three rounds. A negative delta means the lazy-storage experiment took less
time.

| Case | PR 5 ns/item | Branch 6 ns/item | Delta |
| --- | ---: | ---: | ---: |
| Construct/destroy empty default hash | 315.757 | 6.064 | -98.1% |
| Construct, first put, destroy — default size | 300.789 | 244.921 | -18.6% |
| Construct, first put, destroy — requested size 3 | 80.695 | 83.567 | +3.6% |
| Repeated put/erase in retained size-3 storage | 6.941 | 7.728 | +11.3% |
| Resize with four integer/payload mappings | 34.074 | 32.108 | -5.8% |
| Grow `SbHash<int, int>` to 4,096 | 23.193 | 26.846 | +15.8% |
| Grow `SbHash<int, Payload64>` to 4,096 | 56.032 | 45.827 | -18.2% |
| Grow `SbHash<SbString, int>` to 4,096 | 264.656 | 246.814 | -6.7% |
| Integer lookup hit | 1.597 | 1.590 | -0.4% |
| String lookup hit | 75.407 | 75.837 | +0.6% |
| Integer iteration | 1.123 | 1.131 | +0.7% |

The empty-default construction improvement is large and stable: the three
branch-6 medians were 5.781, 6.752, and 5.660 ns/map, versus 315.734, 322.105,
and 309.433 ns/map on PR 5.

Two hot integer cases regressed consistently. `grow_int_int_4096` was slower
in all three rounds by approximately 15.8% overall. Repeated `put`/`erase` was
slower in all three rounds by approximately 11.3% overall. The latter is the
most direct measurement of the extra initialized-storage check on every
`put()`.

As a code-layout control, `grow_int_int_4096` was then repeated five times
with the expanded dedicated benchmark binary. The result remained +16.5%
(23.619 versus 27.522 ns/item), so the growth regression survived
recompilation and a changed binary layout. Two additional `put`/`erase` rounds
reduced that case's combined five-round delta to +8.6%, but branch 6 remained
slower in every individual round.

The other growth cases improved, so this run does not support a claim of a
universal insertion regression. Lookup and iteration remained within 1% and
are treated as unchanged at the noise level. The requested-size-3 first-put
case varied by run and its 3.6% aggregate delta is not treated as conclusive.

## Functional cross-check

All timing checksums matched between revisions. The deterministic resize audit
reported one copy, zero assignments, and zero destructions for both revisions,
so lazy allocation did not undo the relink-resize behavior.

## Memory context

The separate allocation probe recorded 2,160 heap bytes per untouched default
map on PR 5 and 48 bytes per map on branch 6, a reduction of approximately
2,112 bytes per empty map. This timing run therefore exposes the trade-off:
large savings for empty/default-small-map lifetimes, with a measurable cost in
some hot insertion patterns.

## Commands

The benchmark source was compiled twice with identical flags and linked to the
same Coin build; the source include path selected PR 5 or branch 6. Each timing
round used:

```sh
taskset -c 2 ./CoinSbHashBenchmark-REVISION \
  --samples 20 --target-ms 50 --csv
```

The benchmark is a local engineering aid and is not part of the proposed PR
stack.

## Mitigation experiment

The original lazy-storage patch changed two things on destruction: it routed
cleanup through `releaseStorage()` and released the bucket vector before the
entry pool. A minimal follow-up restored the historical release order after
`clear()`:

1. destroy the `cc_memalloc` entry pool;
2. delete the bucket vector.

No lazy-allocation behavior was otherwise changed. Three new alternating
20-sample rounds produced:

| Case | PR 5 ns/item | Lazy + historical release order | Delta |
| --- | ---: | ---: | ---: |
| Construct/destroy empty default hash | 301.887 | 5.567 | -98.2% |
| Construct, first put, destroy — default size | 299.680 | 241.242 | -19.5% |
| Grow `SbHash<int, int>` to 4,096 | 23.208 | 22.526 | -2.9% |
| Grow `SbHash<int, Payload64>` to 4,096 | 51.776 | 51.230 | -1.1% |
| Grow `SbHash<SbString, int>` to 4,096 | 249.022 | 248.550 | -0.2% |
| Repeated put/erase in retained size-3 storage | 6.714 | 6.508 | -3.1% |

This control indicates that the reported hot-path regressions were dominated
by teardown/allocation behavior in the benchmark rather than by the
initialized-storage check in `put()`. The minimal release-order restoration is
therefore the preferred mitigation. A compile-time eager/lazy policy prototype
also removed the direct `put()` overhead, but adds API and template complexity
and is not justified by the corrected measurements.

The result remains allocator- and platform-sensitive. It should be validated
in the Coin test suite and FreeCAD build before the sixth branch is promoted.

The mitigation was then incorporated into the actual candidate commit
`20e8172022` (`fix/sbhash-06-lazy-storage`) and compiled without an overlay.
Three alternating rounds retained the empty-map improvement (-98.2%) and the
first-put improvement (-17.9%). Growth results were +2.3% for `int`/`int`,
-1.9% for the 64-byte payload, and -1.4% for `SbString`; individual
`int`/`int` rounds moved in both directions. A stronger five-round read-only
control measured lookup at +0.3% and iteration at -2.4%, also with per-round
movement in both directions. No stable performance regression remained.

The final candidate passed all 8 tests in its isolated Coin build and all 25
tests in the integration lab. FreeCAD rebuilt its `Coin` and `pivy_coin`
targets, and the headless smoke test returned `SBHASH_FREECAD_OK Separator`.
