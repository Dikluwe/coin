/**************************************************************************\
 * Copyright (c) 2026 FreeCAD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include "misc/SbAdaptiveMap.h"
#include "misc/SbHashName.h"
#include "CoinTest.h"

#include <map>

namespace {

struct CountedKey {
  CountedKey(int valuearg = 0) : value(valuearg) { }
  bool operator==(const CountedKey & other) const { return this->value == other.value; }
  int value;
};

unsigned int hashcalls = 0;

unsigned int
SbHashFunc(const CountedKey & key)
{
  ++hashcalls;
  return static_cast<unsigned int>(key.value);
}

struct CollidingKey {
  explicit CollidingKey(int valuearg) : value(valuearg) { }
  CollidingKey(const CollidingKey & other) : value(other.value) { }
  CollidingKey & operator=(const CollidingKey & other) {
    this->value = other.value;
    return *this;
  }
  bool operator==(const CollidingKey & other) const {
    return this->value == other.value;
  }
  int value;
};

unsigned int
SbHashFunc(const CollidingKey &)
{
  return 0xdeadbeefU;
}

struct TrackedValue {
  TrackedValue(int valuearg = 0) : value(valuearg) { ++alive; }
  TrackedValue(const TrackedValue & other) : value(other.value) { ++alive; }
  ~TrackedValue(void) { --alive; }
  TrackedValue & operator=(const TrackedValue & other) {
    this->value = other.value;
    return *this;
  }

  int value;
  static int alive;
};

int TrackedValue::alive = 0;

template <unsigned int Capacity, unsigned int HashThreshold>
void
exerciseDifferentialSequence(void)
{
  SbAdaptiveMap<int, int, Capacity, HashThreshold> map;
  std::map<int, int> reference;
  unsigned int state = 0x9e3779b9U + Capacity;

  for (int step = 0; step < 1500; ++step) {
    state = state * 1664525U + 1013904223U;
    const int key = static_cast<int>((state >> 16) % 19U) - 9;
    const int value = step * 5 + 3;

    switch (state % 6U) {
    case 0: {
      const bool inserted = reference.find(key) == reference.end();
      reference[key] = value;
      BOOST_CHECK_EQUAL(static_cast<bool>(map.put(key, value)), inserted);
      break;
    }
    case 1: {
      const bool inserted = reference.insert(std::make_pair(key, value)).second;
      BOOST_CHECK_EQUAL(static_cast<bool>(map.insert(key, value)), inserted);
      break;
    }
    case 2:
      BOOST_CHECK_EQUAL(map.erase(key), reference.erase(key));
      break;
    case 3:
      if ((state & 0xffU) == 0U) {
        if ((state & 0x100U) != 0U) map.releaseStorage();
        else map.clear();
        reference.clear();
      }
      break;
    case 4: {
      SbAdaptiveMap<int, int, Capacity, HashThreshold> copy(map);
      BOOST_CHECK_EQUAL(copy.getNumElements(), map.getNumElements());
      break;
    }
    default:
      break;
    }

    BOOST_CHECK_EQUAL(map.getNumElements(),
                      static_cast<unsigned int>(reference.size()));
    for (int candidate = -9; candidate <= 9; ++candidate) {
      int actual = -1;
      const std::map<int, int>::const_iterator expected = reference.find(candidate);
      BOOST_CHECK_EQUAL(static_cast<bool>(map.get(candidate, actual)),
                        expected != reference.end());
      if (expected != reference.end()) BOOST_CHECK_EQUAL(actual, expected->second);
    }
  }
}

} // namespace

BOOST_AUTO_TEST_CASE(SbAdaptiveMap_uses_vector_before_hash)
{
  SbAdaptiveMap<CountedKey, int, 5, 8> map;
  hashcalls = 0;

  for (int key = 0; key < 5; ++key) {
    BOOST_REQUIRE(map.put(CountedKey(key), key * 10));
  }
  BOOST_CHECK_EQUAL(hashcalls, 0U);
  BOOST_CHECK(!map.put(CountedKey(2), 99));
  BOOST_CHECK_EQUAL(hashcalls, 0U);

  BOOST_REQUIRE(map.put(CountedKey(5), 50));
  BOOST_CHECK_EQUAL(hashcalls, 0U);
  BOOST_REQUIRE(map.put(CountedKey(6), 60));
  BOOST_REQUIRE(map.put(CountedKey(7), 70));
  BOOST_CHECK_EQUAL(hashcalls, 0U);
  BOOST_CHECK(!map.put(CountedKey(6), 66));
  BOOST_CHECK_EQUAL(hashcalls, 0U);

  BOOST_REQUIRE(map.put(CountedKey(8), 80));
  BOOST_CHECK(hashcalls >= 9U);
  for (int key = 0; key < 9; ++key) {
    int value = -1;
    BOOST_REQUIRE(map.get(CountedKey(key), value));
    const int expected = key == 2 ? 99 : (key == 6 ? 66 : key * 10);
    BOOST_CHECK_EQUAL(value, expected);
  }

  for (int key = 0; key < 9; ++key) {
    BOOST_CHECK_EQUAL(map.erase(CountedKey(key)), static_cast<size_t>(1));
  }
  BOOST_CHECK_EQUAL(map.getNumElements(), 0U);

  hashcalls = 0;
  BOOST_REQUIRE(map.put(CountedKey(9), 90));
  BOOST_CHECK_EQUAL(hashcalls, 0U);
  for (int key = 10; key < 17; ++key) {
    BOOST_REQUIRE(map.put(CountedKey(key), key * 10));
  }
  BOOST_CHECK_EQUAL(hashcalls, 0U);
  BOOST_CHECK_EQUAL(map.getNumElements(), 8U);
}

BOOST_AUTO_TEST_CASE(SbAdaptiveMap_insert_iteration_and_clear)
{
  SbAdaptiveMap<SbName, int, 1> map;
  BOOST_REQUIRE(map.insert(SbName("first"), 1));
  BOOST_CHECK(!map.insert(SbName("first"), 99));
  BOOST_REQUIRE(map.insert(SbName("second"), 2));

  int first = 0;
  BOOST_REQUIRE(map.get(SbName("first"), first));
  BOOST_CHECK_EQUAL(first, 1);

  int sum = 0;
  map.forEach([&sum](const SbName &, const int & value) { sum += value; });
  BOOST_CHECK_EQUAL(sum, 3);

  SbList<SbName> keys;
  map.makeKeyList(keys);
  BOOST_CHECK_EQUAL(keys.getLength(), 2);

  map.clear();
  BOOST_CHECK_EQUAL(map.getNumElements(), 0U);
  BOOST_REQUIRE(map.put(SbName("reused"), 7));
  BOOST_REQUIRE(map.get(SbName("reused"), first));
  BOOST_CHECK_EQUAL(first, 7);
}

BOOST_AUTO_TEST_CASE(SbAdaptiveMap_copy_assignment_across_modes)
{
  SbAdaptiveMap<int, int, 1, 3> spilled;
  spilled.put(1, 10);
  spilled.put(2, 20);

  SbAdaptiveMap<int, int, 1, 3> copy(spilled);
  spilled.erase(1);
  int value = 0;
  BOOST_REQUIRE(copy.get(1, value));
  BOOST_CHECK_EQUAL(value, 10);

  SbAdaptiveMap<int, int, 1, 3> assigned;
  assigned.put(8, 80);
  assigned = copy;
  SbAdaptiveMap<int, int, 1, 3> * same = &assigned;
  assigned = *same;
  copy.clear();
  BOOST_REQUIRE(assigned.get(2, value));
  BOOST_CHECK_EQUAL(value, 20);

  assigned.put(3, 30);
  assigned.put(4, 40);
  SbAdaptiveMap<int, int, 1, 3> hashcopy(assigned);
  assigned.clear();
  BOOST_REQUIRE(hashcopy.get(4, value));
  BOOST_CHECK_EQUAL(value, 40);
}

BOOST_AUTO_TEST_CASE(SbAdaptiveMap_manages_nontrivial_entry_lifetimes)
{
  const int baseline = TrackedValue::alive;
  {
    TrackedValue one(1);
    TrackedValue two(2);
    TrackedValue three(3);
    const int locals = TrackedValue::alive;

    SbAdaptiveMap<int, TrackedValue, 2> map;
    map.put(1, one);
    map.put(2, two);
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals + 2);
    map.put(3, three);
    BOOST_CHECK_EQUAL(map.getNumElements(), 3U);

    SbAdaptiveMap<int, TrackedValue, 2> copy(map);
    BOOST_CHECK_EQUAL(copy.getNumElements(), 3U);
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals + 6);
    copy.clear();
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals + 3);

    map.erase(2);
    map.clear();
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals);
  }
  BOOST_CHECK_EQUAL(TrackedValue::alive, baseline);
}

BOOST_AUTO_TEST_CASE(SbAdaptiveMap_clear_reuses_and_release_discards_storage)
{
  const int baseline = TrackedValue::alive;
  {
    TrackedValue value(7);
    const int locals = TrackedValue::alive;
    SbAdaptiveMap<int, TrackedValue, 2, 4> map;

    for (int key = 0; key < 3; ++key) map.put(key, value);
    const TrackedValue * firstvectorvalue = NULL;
    map.forEach([&firstvectorvalue](const int & key, const TrackedValue & entry) {
      if (key == 0) firstvectorvalue = &entry;
    });
    BOOST_REQUIRE(firstvectorvalue != NULL);

    map.clear();
    BOOST_CHECK_EQUAL(map.getNumElements(), 0U);
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals);

    for (int key = 0; key < 3; ++key) map.put(key, value);
    const TrackedValue * reusedvectorvalue = NULL;
    map.forEach([&reusedvectorvalue](const int & key, const TrackedValue & entry) {
      if (key == 0) reusedvectorvalue = &entry;
    });
    BOOST_REQUIRE(reusedvectorvalue != NULL);
    BOOST_CHECK_EQUAL(firstvectorvalue, reusedvectorvalue);

    map.put(3, value);
    map.put(4, value);
    BOOST_CHECK_EQUAL(map.getNumElements(), 5U);
    map.clear();
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals);

    for (int key = 0; key < 5; ++key) map.put(key, value);
    BOOST_CHECK_EQUAL(map.getNumElements(), 5U);
    map.releaseStorage();
    BOOST_CHECK_EQUAL(map.getNumElements(), 0U);
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals);

    map.put(9, value);
    BOOST_CHECK_EQUAL(map.getNumElements(), 1U);
  }
  BOOST_CHECK_EQUAL(TrackedValue::alive, baseline);
}

BOOST_AUTO_TEST_CASE(SbRobinHoodHash_handles_collisions_growth_and_backshift)
{
  SbRobinHoodHash<CollidingKey, int> map(4);
  for (int key = 0; key < 128; ++key) {
    BOOST_REQUIRE(map.put(CollidingKey(key), key * 3));
  }
  BOOST_CHECK_EQUAL(map.getNumElements(), 128U);

  for (int key = 0; key < 128; ++key) {
    int value = -1;
    BOOST_REQUIRE(map.get(CollidingKey(key), value));
    BOOST_CHECK_EQUAL(value, key * 3);
  }

  for (int key = 1; key < 128; key += 2) {
    BOOST_CHECK_EQUAL(map.erase(CollidingKey(key)), static_cast<size_t>(1));
  }
  BOOST_CHECK_EQUAL(map.getNumElements(), 64U);

  for (int key = 0; key < 128; ++key) {
    int value = -1;
    BOOST_CHECK_EQUAL(static_cast<bool>(map.get(CollidingKey(key), value)),
                      (key & 1) == 0);
    if ((key & 1) == 0) BOOST_CHECK_EQUAL(value, key * 3);
  }

  SbRobinHoodHash<CollidingKey, int> copy(map);
  map.clear();
  BOOST_CHECK_EQUAL(map.getNumElements(), 0U);
  BOOST_CHECK_EQUAL(copy.getNumElements(), 64U);
  for (int key = 0; key < 128; key += 2) {
    int value = -1;
    BOOST_REQUIRE(copy.get(CollidingKey(key), value));
    BOOST_CHECK_EQUAL(value, key * 3);
  }

  SbRobinHoodHash<CollidingKey, int> assigned;
  assigned = copy;
  assigned = assigned;
  BOOST_CHECK_EQUAL(assigned.getNumElements(), 64U);
}

BOOST_AUTO_TEST_CASE(SbRobinHoodHash_manages_nontrivial_entry_lifetimes)
{
  const int baseline = TrackedValue::alive;
  {
    TrackedValue value(11);
    const int locals = TrackedValue::alive;
    SbRobinHoodHash<int, TrackedValue> map;
    for (int key = 0; key < 100; ++key) map.put(key, value);
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals + 100);
    for (int key = 0; key < 100; key += 2) map.erase(key);
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals + 50);
    map.clear();
    BOOST_CHECK_EQUAL(TrackedValue::alive, locals);
  }
  BOOST_CHECK_EQUAL(TrackedValue::alive, baseline);
}

BOOST_AUTO_TEST_CASE(SbRobinHoodHash_supports_SbHash_style_access)
{
  typedef SbRobinHoodHash<int, int> Map;
  Map map;
  map.reserve(32);
  map[1] = 10;
  map[2] = 20;
  map[3] = 30;

  Map::const_iterator found = map.find(2);
  BOOST_REQUIRE(found != map.const_end());
  BOOST_CHECK_EQUAL(found->obj, 20);
  BOOST_CHECK(map.find(9) == map.const_end());

  int sum = 0;
  unsigned int count = 0;
  for (Map::iterator it = map.begin();
       it != map.end(); ++it) {
    sum += it->obj;
    ++count;
  }
  BOOST_CHECK_EQUAL(count, 3U);
  BOOST_CHECK_EQUAL(sum, 60);

  map.setHashPolicy(Map::SPREAD_HASH);
  BOOST_CHECK_EQUAL(map.getHashPolicy(), Map::SPREAD_HASH);
  BOOST_REQUIRE(map.get(2, sum));
  BOOST_CHECK_EQUAL(sum, 20);

  map.releaseStorage();
  BOOST_CHECK_EQUAL(map.getNumElements(), 0U);
  BOOST_REQUIRE(map.put(4, 40));
  BOOST_REQUIRE(map.get(4, sum));
  BOOST_CHECK_EQUAL(sum, 40);
}

BOOST_AUTO_TEST_CASE(SbRobinHoodHash_selects_mapping_from_observed_hashes)
{
  uint32_t sequential[16];
  uint32_t aligned[16];
  uint32_t alignedlarge[80];
  for (uint32_t i = 0; i < 16; ++i) {
    sequential[i] = i;
    aligned[i] = i * 256U;
  }
  for (uint32_t i = 0; i < 80; ++i) alignedlarge[i] = i * 256U;

  typedef SbRobinHoodHash<unsigned int, int> Map;
  BOOST_CHECK_EQUAL(Map::chooseHashPolicy(sequential, 16, 17),
                    Map::DIRECT_HASH);
  BOOST_CHECK_EQUAL(Map::chooseHashPolicy(aligned, 16, 17),
                    Map::SPREAD_HASH);
  BOOST_CHECK_EQUAL(Map::chooseHashPolicy(alignedlarge, 80, 81),
                    Map::SPREAD_HASH);
}

BOOST_AUTO_TEST_CASE(SbRobinHoodHash_direct_mapping_self_corrects)
{
  typedef SbRobinHoodHash<unsigned int, int> Map;
  Map sequential(128, Map::DIRECT_HASH);
  for (unsigned int key = 0; key < 128; ++key) {
    sequential.put(key, static_cast<int>(key));
  }
  BOOST_CHECK_EQUAL(sequential.getHashPolicy(), Map::DIRECT_HASH);

  Map aligned(128, Map::DIRECT_HASH);
  for (unsigned int key = 0; key < 128; ++key) {
    aligned.put(key * 256U, static_cast<int>(key));
  }
  BOOST_CHECK_EQUAL(aligned.getHashPolicy(), Map::SPREAD_HASH);
  for (unsigned int key = 0; key < 128; ++key) {
    int value = -1;
    BOOST_REQUIRE(aligned.get(key * 256U, value));
    BOOST_CHECK_EQUAL(value, static_cast<int>(key));
  }
}

BOOST_AUTO_TEST_CASE(SbAdaptiveMap_matches_std_map_at_scxml_capacities)
{
  exerciseDifferentialSequence<1, 4>();
  exerciseDifferentialSequence<5, 12>();
  exerciseDifferentialSequence<7, 16>();
}
