#include <Inventor/SbString.h>

#include "misc/SbHash.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

volatile std::uint64_t benchmarkSink = 0;

struct Payload {
  Payload(int seed = 0)
  {
    for (std::size_t i = 0; i < sizeof(this->words) / sizeof(this->words[0]); ++i) {
      this->words[i] = static_cast<std::uint64_t>(seed) * 1315423911u + i;
    }
  }

  std::uint64_t words[8];
};

struct CountedPayload {
  CountedPayload(int value = 0) : value(value) {}

  CountedPayload(const CountedPayload & other) : value(other.value)
  {
    ++CountedPayload::copies;
  }

  CountedPayload & operator=(const CountedPayload & other)
  {
    this->value = other.value;
    ++CountedPayload::assignments;
    return *this;
  }

  ~CountedPayload()
  {
    ++CountedPayload::destructions;
  }

  static void reset()
  {
    CountedPayload::copies = 0;
    CountedPayload::assignments = 0;
    CountedPayload::destructions = 0;
  }

  int value;
  static std::uint64_t copies;
  static std::uint64_t assignments;
  static std::uint64_t destructions;
};

std::uint64_t CountedPayload::copies = 0;
std::uint64_t CountedPayload::assignments = 0;
std::uint64_t CountedPayload::destructions = 0;

struct Options {
  Options() : samples(15), targetMilliseconds(40), csv(false) {}

  int samples;
  int targetMilliseconds;
  bool csv;
  std::string filter;
};

struct BenchmarkCase {
  std::string name;
  std::uint64_t itemsPerRun;
  std::function<std::uint64_t(void)> function;
};

struct Result {
  std::string name;
  std::uint64_t itemsPerRun;
  std::uint64_t repetitions;
  double medianNanosecondsPerItem;
  double p95NanosecondsPerItem;
  std::uint64_t checksum;
};

std::uint64_t parsePositive(const char * value, const char * option)
{
  char * end = NULL;
  const unsigned long long parsed = std::strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0) {
    std::cerr << option << " requires a positive integer\n";
    std::exit(2);
  }
  return static_cast<std::uint64_t>(parsed);
}

Options parseOptions(int argc, char ** argv)
{
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--csv") {
      options.csv = true;
    }
    else if (argument == "--samples" && i + 1 < argc) {
      options.samples = static_cast<int>(parsePositive(argv[++i], "--samples"));
    }
    else if (argument == "--target-ms" && i + 1 < argc) {
      options.targetMilliseconds = static_cast<int>(parsePositive(argv[++i], "--target-ms"));
    }
    else if (argument == "--filter" && i + 1 < argc) {
      options.filter = argv[++i];
    }
    else if (argument == "--help") {
      std::cout << "Usage: CoinSbHashBenchmark [--samples N] [--target-ms N] "
                   "[--filter TEXT] [--csv]\n";
      std::exit(0);
    }
    else {
      std::cerr << "Unknown or incomplete option: " << argument << '\n';
      std::exit(2);
    }
  }
  return options;
}

double percentile(std::vector<double> values, double fraction)
{
  std::sort(values.begin(), values.end());
  const double index = fraction * static_cast<double>(values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(index);
  const std::size_t upper = std::min(lower + 1, values.size() - 1);
  const double weight = index - static_cast<double>(lower);
  return values[lower] * (1.0 - weight) + values[upper] * weight;
}

std::uint64_t runRepeated(const BenchmarkCase & benchmark, std::uint64_t repetitions)
{
  std::uint64_t checksum = 0;
  for (std::uint64_t i = 0; i < repetitions; ++i) {
    checksum += benchmark.function();
  }
  benchmarkSink ^= checksum;
  return checksum;
}

Result measure(const BenchmarkCase & benchmark, const Options & options)
{
  typedef std::chrono::steady_clock Clock;
  const std::chrono::milliseconds target(options.targetMilliseconds);

  runRepeated(benchmark, 1);

  std::uint64_t repetitions = 1;
  for (;;) {
    const Clock::time_point start = Clock::now();
    runRepeated(benchmark, repetitions);
    const Clock::duration elapsed = Clock::now() - start;
    if (elapsed >= target || repetitions >= (std::uint64_t(1) << 30)) break;
    repetitions *= 2;
  }

  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(options.samples));
  for (int sample = 0; sample < options.samples; ++sample) {
    const Clock::time_point start = Clock::now();
    runRepeated(benchmark, repetitions);
    const std::chrono::duration<double, std::nano> elapsed = Clock::now() - start;
    const double items = static_cast<double>(repetitions) *
                         static_cast<double>(benchmark.itemsPerRun);
    samples.push_back(elapsed.count() / items);
  }

  Result result;
  result.name = benchmark.name;
  result.itemsPerRun = benchmark.itemsPerRun;
  result.repetitions = repetitions;
  result.medianNanosecondsPerItem = percentile(samples, 0.5);
  result.p95NanosecondsPerItem = percentile(samples, 0.95);
  result.checksum = benchmark.function();
  benchmarkSink ^= result.checksum;
  return result;
}

std::vector<SbString> makeStringKeys(std::size_t count)
{
  std::vector<SbString> keys;
  keys.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::string text = "sbhash-key-" + std::to_string(i) + "-";
    text.append(48, static_cast<char>('a' + (i % 26)));
    keys.push_back(SbString(text.c_str()));
  }
  return keys;
}

void printCopyAudit(bool csv)
{
  SbHash<int, CountedPayload> hash(3);
  for (int i = 0; i < 3; ++i) {
    const CountedPayload value(i);
    hash.put(i, value);
  }

  CountedPayload::reset();
  const CountedPayload fourth(3);
  hash.put(3, fourth);

  if (csv) {
    std::cout << "audit,resize_copy_audit,,,,,,"
              << CountedPayload::copies << ','
              << CountedPayload::assignments << ','
              << CountedPayload::destructions << '\n';
  }
  else {
    std::cout << "resize_copy_audit: copies=" << CountedPayload::copies
              << " assignments=" << CountedPayload::assignments
              << " destructions=" << CountedPayload::destructions << '\n';
  }
}

} // namespace

int main(int argc, char ** argv)
{
  const Options options = parseOptions(argc, argv);
  const std::size_t lookupSize = 4096;
  const std::vector<SbString> stringKeys = makeStringKeys(lookupSize);

  SbHash<int, int> integerLookup(static_cast<unsigned int>(lookupSize * 2));
  SbHash<SbString, int> stringLookup(static_cast<unsigned int>(lookupSize * 2));
  for (std::size_t i = 0; i < lookupSize; ++i) {
    integerLookup.put(static_cast<int>(i), static_cast<int>(i));
    stringLookup.put(stringKeys[i], static_cast<int>(i));
  }

  std::vector<BenchmarkCase> benchmarks;

  benchmarks.push_back(BenchmarkCase{
    "resize_int_payload_4", 4,
    []() -> std::uint64_t {
      SbHash<int, Payload> hash(3);
      for (int i = 0; i < 4; ++i) hash.put(i, Payload(i));
      return hash.getNumElements();
    }});

  benchmarks.push_back(BenchmarkCase{
    "grow_int_int_4096", 4096,
    []() -> std::uint64_t {
      SbHash<int, int> hash(3);
      for (int i = 0; i < 4096; ++i) hash.put(i, i);
      return hash.getNumElements();
    }});

  benchmarks.push_back(BenchmarkCase{
    "grow_int_payload_4096", 4096,
    []() -> std::uint64_t {
      SbHash<int, Payload> hash(3);
      for (int i = 0; i < 4096; ++i) hash.put(i, Payload(i));
      return hash.getNumElements();
    }});

  benchmarks.push_back(BenchmarkCase{
    "grow_string_int_4096", 4096,
    [&stringKeys]() -> std::uint64_t {
      SbHash<SbString, int> hash(3);
      for (std::size_t i = 0; i < stringKeys.size(); ++i) {
        hash.put(stringKeys[i], static_cast<int>(i));
      }
      return hash.getNumElements();
    }});

  benchmarks.push_back(BenchmarkCase{
    "lookup_int_hit_4096", 4096,
    [&integerLookup]() -> std::uint64_t {
      std::uint64_t sum = 0;
      int value = 0;
      for (int i = 0; i < 4096; ++i) {
        if (integerLookup.get(i, value)) sum += static_cast<std::uint64_t>(value);
      }
      return sum;
    }});

  benchmarks.push_back(BenchmarkCase{
    "lookup_string_hit_4096", 4096,
    [&stringLookup, &stringKeys]() -> std::uint64_t {
      std::uint64_t sum = 0;
      int value = 0;
      for (std::size_t i = 0; i < stringKeys.size(); ++i) {
        if (stringLookup.get(stringKeys[i], value)) {
          sum += static_cast<std::uint64_t>(value);
        }
      }
      return sum;
    }});

  benchmarks.push_back(BenchmarkCase{
    "iterate_int_4096", 4096,
    [&integerLookup]() -> std::uint64_t {
      std::uint64_t sum = 0;
      for (SbHash<int, int>::const_iterator it = integerLookup.const_begin();
           it != integerLookup.const_end(); ++it) {
        sum += static_cast<std::uint64_t>(it->obj);
      }
      return sum;
    }});

  if (options.csv) {
    std::cout << "record_type,name,median_ns_per_item,p95_ns_per_item,"
                 "items_per_run,repetitions,checksum,copies,assignments,"
                 "destructions\n";
  }
  else {
    std::cout << std::left << std::setw(29) << "benchmark"
              << std::right << std::setw(14) << "median ns"
              << std::setw(14) << "p95 ns"
              << std::setw(12) << "repetitions" << '\n';
  }

  for (std::size_t i = 0; i < benchmarks.size(); ++i) {
    if (!options.filter.empty() &&
        benchmarks[i].name.find(options.filter) == std::string::npos) {
      continue;
    }
    const Result result = measure(benchmarks[i], options);
    if (options.csv) {
      std::cout << "timing," << result.name << ','
                << std::fixed << std::setprecision(3)
                << result.medianNanosecondsPerItem << ','
                << result.p95NanosecondsPerItem << ','
                << result.itemsPerRun << ',' << result.repetitions << ','
                << result.checksum << ",,,\n";
    }
    else {
      std::cout << std::left << std::setw(29) << result.name
                << std::right << std::fixed << std::setprecision(3)
                << std::setw(14) << result.medianNanosecondsPerItem
                << std::setw(14) << result.p95NanosecondsPerItem
                << std::setw(12) << result.repetitions << '\n';
    }
  }

  if (options.filter.empty() ||
      std::string("resize_copy_audit").find(options.filter) != std::string::npos) {
    printCopyAudit(options.csv);
  }

  return benchmarkSink == std::uint64_t(-1) ? 1 : 0;
}
