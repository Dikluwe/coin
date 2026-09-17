# Coin microbenchmarks

Benchmarks are deliberately opt-in and are not registered as tests. Configure
a release build with:

```sh
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DCOIN_BUILD_TESTS=OFF \
  -DCOIN_BUILD_BENCHMARKS=ON
cmake --build build-bench --target CoinSbHashBenchmark -j4
build-bench/bin/CoinSbHashBenchmark
```

The `SbHash` benchmark reports the median and p95 from repeated samples. Each
sample is automatically calibrated to run for approximately the requested
duration. Use `--csv` for machine-readable output, for example:

```sh
taskset -c 2 build-bench/bin/CoinSbHashBenchmark \
  --samples 20 --target-ms 50 --csv > sbhash-results.csv
```

For an implementation comparison, compile the same benchmark source and build
configuration against both revisions. Run them on the same idle machine, pin
them to the same CPU, and alternate their execution order. The checksum column
must match between revisions. `resize_copy_audit` is deterministic and is not
a timing result: it records copies and destructions caused by the fourth
insertion into a hash requested with size 3.

Useful supplemental hardware counters on Linux can be collected with:

```sh
perf stat -r 10 -e cycles,instructions,cache-references,cache-misses \
  taskset -c 2 build-bench/bin/CoinSbHashBenchmark \
  --samples 5 --target-ms 20 --filter grow_int_payload_4096
```
