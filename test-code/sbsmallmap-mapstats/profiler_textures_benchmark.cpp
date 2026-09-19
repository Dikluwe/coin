#include "misc/SbSmallMap.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <new>
#include <vector>

namespace {
  volatile unsigned long long checksum = 0;
  volatile bool countallocations = false;
  volatile size_t allocations = 0;

  void * allocate(size_t size)
  {
    if (countallocations) ++allocations;
    void * memory = std::malloc(size);
    if (memory == NULL) throw std::bad_alloc();
    return memory;
  }

  typedef const int * Key;
  typedef int * Value;
  typedef SbSmallMap<Key, Value> SmallMap;
  typedef std::map<Key, Value> TreeMap;

  int keysstorage[6];
  int values[6];
  Key keys[6] = {
    &keysstorage[0], &keysstorage[1], &keysstorage[2],
    &keysstorage[3], &keysstorage[4], &keysstorage[5]
  };

  template <typename Function>
  double medianNanoseconds(Function function, int samples)
  {
    std::vector<double> timings;
    timings.reserve(static_cast<size_t>(samples));
    for (int sample = 0; sample < samples; ++sample) {
      const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
      function();
      const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now();
      timings.push_back(std::chrono::duration<double, std::nano>(end - start).count());
    }
    std::sort(timings.begin(), timings.end());
    return timings[timings.size() / 2];
  }

  void runSmallLifecycle(int iterations)
  {
    unsigned long long sum = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      SmallMap map;
      for (int index = 0; index < 6; ++index) map.put(keys[index], &values[index]);
      for (int repeat = 0; repeat < 3; ++repeat) {
        for (int index = 0; index < 6; ++index) {
          Value value = NULL;
          map.get(keys[index], value);
          sum += static_cast<unsigned long long>(*value + repeat);
        }
      }
      for (SmallMap::const_iterator it = map.const_begin();
           it != map.const_end(); ++it) sum += static_cast<unsigned long long>(*it->obj);
      map.clear();
    }
    checksum += sum;
  }

  void runTreeLifecycle(int iterations)
  {
    unsigned long long sum = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      TreeMap map;
      for (int index = 0; index < 6; ++index) map[keys[index]] = &values[index];
      for (int repeat = 0; repeat < 3; ++repeat) {
        for (int index = 0; index < 6; ++index) {
          const TreeMap::const_iterator found = map.find(keys[index]);
          sum += static_cast<unsigned long long>(*found->second + repeat);
        }
      }
      for (TreeMap::const_iterator it = map.begin(); it != map.end(); ++it) {
        sum += static_cast<unsigned long long>(*it->second);
      }
      map.clear();
    }
    checksum += sum;
  }

  void runSmallLookups(int iterations)
  {
    SmallMap map;
    for (int index = 0; index < 6; ++index) map.put(keys[index], &values[index]);
    unsigned long long sum = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      Value value = NULL;
      map.get(keys[iteration % 6], value);
      sum += static_cast<unsigned long long>(*value);
    }
    checksum += sum;
  }

  void runTreeLookups(int iterations)
  {
    TreeMap map;
    for (int index = 0; index < 6; ++index) map[keys[index]] = &values[index];
    unsigned long long sum = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      sum += static_cast<unsigned long long>(*map.find(keys[iteration % 6])->second);
    }
    checksum += sum;
  }
}

void * operator new(std::size_t size) { return allocate(size); }
void * operator new[](std::size_t size) { return allocate(size); }
void operator delete(void * ptr) noexcept { std::free(ptr); }
void operator delete[](void * ptr) noexcept { std::free(ptr); }

int main(void)
{
  for (int index = 0; index < 6; ++index) values[index] = index + 1;

  const int lifecycleiterations = 500000;
  const int lookupiterations = 20000000;
  const int samples = 7;

  runSmallLifecycle(1);
  runTreeLifecycle(1);

  allocations = 0;
  countallocations = true;
  runSmallLifecycle(1000);
  countallocations = false;
  const size_t smallallocations = allocations;

  allocations = 0;
  countallocations = true;
  runTreeLifecycle(1000);
  countallocations = false;
  const size_t treeallocations = allocations;

  const double smalllifecycle = medianNanoseconds(
    [=]() { runSmallLifecycle(lifecycleiterations); }, samples) / lifecycleiterations;
  const double treelifecycle = medianNanoseconds(
    [=]() { runTreeLifecycle(lifecycleiterations); }, samples) / lifecycleiterations;
  const double smalllookup = medianNanoseconds(
    [=]() { runSmallLookups(lookupiterations); }, samples) / lookupiterations;
  const double treelookup = medianNanoseconds(
    [=]() { runTreeLookups(lookupiterations); }, samples) / lookupiterations;

  std::printf("six-entry profiler texture pattern\n");
  std::printf("SbSmallMap lifecycle %.2f ns, lookup %.2f ns, allocations/cycle %.2f\n",
              smalllifecycle, smalllookup, smallallocations / 1000.0);
  std::printf("std::map    lifecycle %.2f ns, lookup %.2f ns, allocations/cycle %.2f\n",
              treelifecycle, treelookup, treeallocations / 1000.0);
  std::printf("checksum %llu\n", checksum);
  return 0;
}
