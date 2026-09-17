# SbHash relink benchmark — 2026-09-17

## Compared revisions

- Baseline: `75629da494` (`3a11333e1e^`)
- Relink fix: `3a11333e1e`
- Compiler: GCC 13.3.0
- Configuration: `Release`, `-O3 -DNDEBUG`, C++11

Both executables were compiled from the same `SbHashBenchmark.cpp`. Only their
Coin source and linked `libCoin` differed. Runs were pinned to CPU 2. Two
20-sample, 50 ms target-duration rounds were executed in alternating order:
baseline/fix, then fix/baseline.

## Timing summary

Values are the arithmetic mean of the median nanoseconds per item from the two
rounds. A negative delta means the relink implementation took less time.

| Case | Baseline ns/item | Relink ns/item | Delta |
| --- | ---: | ---: | ---: |
| Resize with four integer/payload mappings | 36.901 | 33.614 | -8.9% |
| Grow `SbHash<int, int>` to 4,096 | 32.874 | 23.673 | -28.0% |
| Grow `SbHash<int, Payload64>` to 4,096 | 67.812 | 54.977 | -18.9% |
| Grow `SbHash<SbString, int>` to 4,096 | 303.847 | 272.604 | -10.3% |

The growth cases improved in both run orders. Lookup and iteration results did
not show the same stability: their medians moved in both directions between
rounds even though the relevant lookup implementation is unchanged. No
performance conclusion is drawn for those operations from this run.

## Deterministic resize audit

The audit resets its counters immediately before the fourth insertion into an
`SbHash<int, CountedPayload>` requested with size 3. That insertion crosses the
resize threshold.

| Revision | Copies | Assignments | Destructions during insertion |
| --- | ---: | ---: | ---: |
| Baseline | 5 | 0 | 4 |
| Relink fix | 1 | 0 | 0 |

The remaining copy is the expected construction of the newly inserted value.
The relink resize itself performs no value copies or destructions.

## Commands

```sh
taskset -c 2 ./CoinSbHashBenchmark-base \
  --samples 20 --target-ms 50 --csv
taskset -c 2 ./CoinSbHashBenchmark-fix \
  --samples 20 --target-ms 50 --csv
```

Hardware-counter collection with `perf stat` was attempted but was unavailable
on this host because `/proc/sys/kernel/perf_event_paranoid` is set to 4. No
system setting was changed for the benchmark.
