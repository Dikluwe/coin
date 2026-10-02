#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef TRACK_ALLOC
// Lab-only Linux/glibc instrumentation: this mode interposes allocation
// functions through __libc_* and malloc_usable_size(). It is intentionally
// not portable production code; compile it only as the standalone lab probe.
#include <malloc.h>

extern "C" void * __libc_malloc(size_t);
extern "C" void * __libc_calloc(size_t, size_t);
extern "C" void * __libc_realloc(void *, size_t);
extern "C" void __libc_free(void *);

static std::atomic<long long> tracked_bytes(0);
static std::atomic<long long> tracked_blocks(0);

extern "C" void * malloc(size_t size)
{
  void * ptr = __libc_malloc(size);
  if (ptr != NULL) {
    tracked_bytes.fetch_add(static_cast<long long>(malloc_usable_size(ptr)),
                            std::memory_order_relaxed);
    tracked_blocks.fetch_add(1, std::memory_order_relaxed);
  }
  return ptr;
}

extern "C" void free(void * ptr)
{
  if (ptr != NULL) {
    tracked_bytes.fetch_sub(static_cast<long long>(malloc_usable_size(ptr)),
                            std::memory_order_relaxed);
    tracked_blocks.fetch_sub(1, std::memory_order_relaxed);
  }
  __libc_free(ptr);
}

extern "C" void * calloc(size_t count, size_t size)
{
  void * ptr = __libc_calloc(count, size);
  if (ptr != NULL) {
    tracked_bytes.fetch_add(static_cast<long long>(malloc_usable_size(ptr)),
                            std::memory_order_relaxed);
    tracked_blocks.fetch_add(1, std::memory_order_relaxed);
  }
  return ptr;
}

extern "C" void * realloc(void * oldptr, size_t size)
{
  long long oldbytes = 0;
  if (oldptr != NULL) {
    oldbytes = static_cast<long long>(malloc_usable_size(oldptr));
  }
  void * ptr = __libc_realloc(oldptr, size);
  if (ptr != NULL) {
    const long long newbytes =
      static_cast<long long>(malloc_usable_size(ptr));
    tracked_bytes.fetch_add(newbytes - oldbytes, std::memory_order_relaxed);
    if (oldptr == NULL) tracked_blocks.fetch_add(1, std::memory_order_relaxed);
  }
  return ptr;
}
#endif

#define COIN_INTERNAL 1
#include "misc/SbHash.h"
#include "misc/SbSmallMap.h"

struct Programs {
  uint32_t dirlight;
  uint32_t pointlight;
  uint32_t fragment;
};

using HashMap = SbHash<uint32_t, Programs>;
using SmallMap = SbSmallMap<uint32_t, Programs>;

struct HashBothOwner {
  HashMap diffuse;
  HashMap specular;
};

struct SmallBothOwner {
  SmallMap diffuse;
  SmallMap specular;
};

struct SpecSmallOwner {
  HashMap diffuse;
  SmallMap specular;
};

static volatile uint64_t checksum = 0;

template <typename T>
inline void escape(T & value)
{
  asm volatile("" : : "g"(&value) : "memory");
}

inline void clobber(void)
{
  asm volatile("" : : : "memory");
}

template <typename Map>
inline uint64_t bump_workload(Map & map)
{
  const Programs one = { 101u, 102u, 103u };
  const Programs two = { 201u, 202u, 203u };
  Programs out = { 0u, 0u, 0u };
  uint64_t sum = 0;

  // Seven gets model two first-use misses plus repeated rendering hits.
  sum += static_cast<uint64_t>(map.get(11u, out));
  clobber();
  sum += static_cast<uint64_t>(map.put(11u, one));
  clobber();
  sum += static_cast<uint64_t>(map.get(11u, out));
  sum += out.fragment;
  clobber();
  sum += static_cast<uint64_t>(map.get(22u, out));
  clobber();
  sum += static_cast<uint64_t>(map.put(22u, two));
  clobber();
  sum += static_cast<uint64_t>(map.get(22u, out));
  sum += out.fragment;
  clobber();
  sum += static_cast<uint64_t>(map.get(11u, out));
  sum += out.dirlight;
  clobber();
  sum += static_cast<uint64_t>(map.get(22u, out));
  sum += out.pointlight;
  clobber();
  sum += static_cast<uint64_t>(map.get(11u, out));
  sum += out.fragment;
  clobber();

  sum += map.erase(22u);
  clobber();
  for (typename Map::const_iterator iter = map.const_begin();
       iter != map.const_end(); ++iter) {
    sum += iter->key + iter->obj.dirlight + iter->obj.pointlight +
      iter->obj.fragment;
  }
  map.clear();
  return sum;
}

template <typename Owner>
double cold_owner_sample(const uint64_t iterations)
{
  uint64_t sum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < iterations; ++i) {
    Owner owner;
    escape(owner);
    sum += bump_workload(owner.specular);
    clobber();
  }
  const auto stop = std::chrono::steady_clock::now();
  checksum += sum;
  return std::chrono::duration<double, std::nano>(stop - start).count() /
    static_cast<double>(iterations);
}

template <typename Map>
double warm_workload_sample(const uint64_t iterations)
{
  Map map;
  escape(map);
  uint64_t sum = 0;
  // Prime any one-time allocation, then retain it across clear(), matching
  // the container's reuse semantics.
  sum += bump_workload(map);
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < iterations; ++i) {
    sum += bump_workload(map);
    clobber();
  }
  const auto stop = std::chrono::steady_clock::now();
  checksum += sum;
  return std::chrono::duration<double, std::nano>(stop - start).count() /
    static_cast<double>(iterations);
}

template <typename Map>
double lookup_sample(const unsigned int entries, const uint64_t iterations)
{
  Map map;
  const Programs one = { 101u, 102u, 103u };
  const Programs two = { 201u, 202u, 203u };
  map.put(11u, one);
  if (entries == 2) map.put(22u, two);
  Programs out = { 0u, 0u, 0u };
  uint64_t sum = 0;
  escape(map);
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < iterations; ++i) {
    const uint32_t key = entries == 1 ? 11u : ((i & 1u) ? 22u : 11u);
    sum += static_cast<uint64_t>(map.get(key, out));
    sum += out.fragment;
    clobber();
  }
  const auto stop = std::chrono::steady_clock::now();
  checksum += sum;
  return std::chrono::duration<double, std::nano>(stop - start).count() /
    static_cast<double>(iterations);
}

template <typename Fn>
void report_samples(const char * name, Fn fn, const unsigned int rounds)
{
  std::vector<double> samples;
  samples.reserve(rounds);
  // Warm code/data and discard the first timing.
  (void) fn();
  for (unsigned int i = 0; i < rounds; ++i) samples.push_back(fn());
  std::sort(samples.begin(), samples.end());
  const double median = samples[samples.size() / 2];
  std::printf("%-30s median=%9.3f ns min=%9.3f max=%9.3f samples=",
              name, median, samples.front(), samples.back());
  for (size_t i = 0; i < samples.size(); ++i) {
    std::printf("%s%.3f", i == 0 ? "" : ",", samples[i]);
  }
  std::printf("\n");
}

#ifdef TRACK_ALLOC
struct AllocationSnapshot {
  long long bytes;
  long long blocks;
};

static AllocationSnapshot alloc_snapshot(void)
{
  AllocationSnapshot result;
  result.bytes = tracked_bytes.load(std::memory_order_relaxed);
  result.blocks = tracked_blocks.load(std::memory_order_relaxed);
  return result;
}

static AllocationSnapshot alloc_delta(const AllocationSnapshot & after,
                                      const AllocationSnapshot & before)
{
  AllocationSnapshot result;
  result.bytes = after.bytes - before.bytes;
  result.blocks = after.blocks - before.blocks;
  return result;
}

template <typename Map>
void report_map_memory(const char * name)
{
  const Programs one = { 101u, 102u, 103u };
  const Programs two = { 201u, 202u, 203u };
  AllocationSnapshot base;
  AllocationSnapshot constructed;
  AllocationSnapshot putone;
  AllocationSnapshot puttwo;
  AllocationSnapshot cleared;
  AllocationSnapshot destroyed;
  {
    base = alloc_snapshot();
    Map map;
    escape(map);
    constructed = alloc_snapshot();
    map.put(11u, one);
    putone = alloc_snapshot();
    map.put(22u, two);
    puttwo = alloc_snapshot();
    map.clear();
    cleared = alloc_snapshot();
  }
  destroyed = alloc_snapshot();
  const AllocationSnapshot ctor_delta = alloc_delta(constructed, base);
  const AllocationSnapshot one_delta = alloc_delta(putone, base);
  const AllocationSnapshot two_delta = alloc_delta(puttwo, base);
  const AllocationSnapshot clear_delta = alloc_delta(cleared, base);
  const AllocationSnapshot destroyed_delta = alloc_delta(destroyed, base);
  std::printf("%-12s sizeof=%zu ctor=%lldB/%lld allocs one=%lldB/%lld allocs "
              "two=%lldB/%lld allocs after-clear=%lldB/%lld allocs "
              "after-dtor=%lldB/%lld allocs\n",
              name, sizeof(Map), ctor_delta.bytes, ctor_delta.blocks,
              one_delta.bytes, one_delta.blocks,
              two_delta.bytes, two_delta.blocks,
              clear_delta.bytes, clear_delta.blocks,
              destroyed_delta.bytes, destroyed_delta.blocks);
}

static int memory_main(void)
{
  // Finish stdio initialization before establishing allocation baselines.
  std::printf("map allocation deltas (malloc_usable_size):\n");
  std::fflush(stdout);
  report_map_memory<HashMap>("SbHash");
  report_map_memory<SmallMap>("SbSmallMap");
  std::printf("owner sizeof: hash+hash=%zu hash+small=%zu small+small=%zu\n",
              sizeof(HashBothOwner), sizeof(SpecSmallOwner),
              sizeof(SmallBothOwner));
  std::printf("entry sizeof: SbHashEntry=%zu SmallMap::Entry=%zu Programs=%zu\n",
              sizeof(HashMap::SbHashEntry), sizeof(SmallMap::Entry),
              sizeof(Programs));
  return 0;
}
#endif

int main(int argc, char ** argv)
{
#ifdef TRACK_ALLOC
  (void) argc;
  (void) argv;
  return memory_main();
#else
  const unsigned int rounds = argc > 1 ?
    static_cast<unsigned int>(std::strtoul(argv[1], NULL, 10)) : 11u;
  const uint64_t cold_iterations = argc > 2 ?
    static_cast<uint64_t>(std::strtoull(argv[2], NULL, 10)) : 300000u;
  const uint64_t warm_iterations = argc > 3 ?
    static_cast<uint64_t>(std::strtoull(argv[3], NULL, 10)) : 1000000u;
  const uint64_t lookup_iterations = argc > 4 ?
    static_cast<uint64_t>(std::strtoull(argv[4], NULL, 10)) : 12000000u;

  std::printf("rounds=%u cold=%" PRIu64 " warm=%" PRIu64
              " lookup=%" PRIu64 "\n", rounds, cold_iterations,
              warm_iterations, lookup_iterations);
  std::printf("sizeof Programs=%zu SbHash=%zu SbSmallMap=%zu owners: "
              "hash+hash=%zu hash+small=%zu small+small=%zu\n",
              sizeof(Programs), sizeof(HashMap), sizeof(SmallMap),
              sizeof(HashBothOwner), sizeof(SpecSmallOwner),
              sizeof(SmallBothOwner));

  report_samples("cold owner: hash+hash", [=]() {
      return cold_owner_sample<HashBothOwner>(cold_iterations);
    }, rounds);
  report_samples("cold owner: hash+small", [=]() {
      return cold_owner_sample<SpecSmallOwner>(cold_iterations);
    }, rounds);
  report_samples("cold owner: small+small", [=]() {
      return cold_owner_sample<SmallBothOwner>(cold_iterations);
    }, rounds);
  report_samples("warm workload: SbHash", [=]() {
      return warm_workload_sample<HashMap>(warm_iterations);
    }, rounds);
  report_samples("warm workload: SbSmallMap", [=]() {
      return warm_workload_sample<SmallMap>(warm_iterations);
    }, rounds);
  report_samples("lookup 1: SbHash", [=]() {
      return lookup_sample<HashMap>(1u, lookup_iterations);
    }, rounds);
  report_samples("lookup 1: SbSmallMap", [=]() {
      return lookup_sample<SmallMap>(1u, lookup_iterations);
    }, rounds);
  report_samples("lookup 2: SbHash", [=]() {
      return lookup_sample<HashMap>(2u, lookup_iterations);
    }, rounds);
  report_samples("lookup 2: SbSmallMap", [=]() {
      return lookup_sample<SmallMap>(2u, lookup_iterations);
    }, rounds);
  std::printf("checksum=%" PRIu64 "\n", checksum);
  return 0;
#endif
}
