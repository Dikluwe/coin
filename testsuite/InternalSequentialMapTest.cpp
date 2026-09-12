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

#include "misc/SbSequentialMap.h"

#include <map>

#include "CoinTest.h"

BOOST_AUTO_TEST_CASE(SbSequentialMap_selects_layout_from_key_density)
{
  SbSequentialMap<int16_t, int> sequential;
  for (int16_t key = 100; key < 108; ++key) sequential.put(key, key * 2);
  BOOST_CHECK(sequential.isDenseMode());
  BOOST_CHECK_EQUAL(*sequential.getPtr(104), 208);

  SbSequentialMap<int16_t, int> sparse;
  for (int16_t key = 100; key < 900; key += 100) sparse.put(key, key * 2);
  BOOST_CHECK(!sparse.isDenseMode());
  BOOST_CHECK_EQUAL(*sparse.getPtr(500), 1000);

  sequential.put(1000, 1);
  BOOST_CHECK(!sequential.isDenseMode());
  BOOST_CHECK_EQUAL(*sequential.getPtr(1000), 1);
  BOOST_CHECK_EQUAL(*sequential.getPtr(104), 208);
}

BOOST_AUTO_TEST_CASE(SbSequentialMap_direct_lookup_and_ordered_keys)
{
  SbSequentialMap<int16_t, int> map;
  BOOST_CHECK(map.put(7, 70));
  BOOST_CHECK(map.put(2, 20));
  BOOST_CHECK(map.put(31, 310));
  BOOST_CHECK(!map.put(7, 71));
  BOOST_CHECK_EQUAL(map.getNumElements(), 3U);

  int value = 0;
  BOOST_CHECK(map.get(7, value));
  BOOST_CHECK_EQUAL(value, 71);
  BOOST_CHECK(!map.get(6, value));
  BOOST_CHECK(map.getPtr(-1) == NULL);

  SbList<int16_t> keys;
  map.makeKeyList(keys);
  BOOST_REQUIRE_EQUAL(keys.getLength(), 3);
  BOOST_CHECK_EQUAL(keys[0], 2);
  BOOST_CHECK_EQUAL(keys[1], 7);
  BOOST_CHECK_EQUAL(keys[2], 31);
}

BOOST_AUTO_TEST_CASE(SbSequentialMap_subscript_erase_copy_and_clear)
{
  SbSequentialMap<uint16_t, int> map;
  map[4] += 3;
  map[9] = 12;
  BOOST_CHECK_EQUAL(*map.getPtr(4), 3);
  BOOST_CHECK_EQUAL(map.erase(4), 1U);
  BOOST_CHECK_EQUAL(map.erase(4), 0U);
  BOOST_CHECK(map.getPtr(4) == NULL);

  SbSequentialMap<uint16_t, int> copy(map);
  BOOST_CHECK_EQUAL(*copy.getPtr(9), 12);
  copy.clear();
  BOOST_CHECK_EQUAL(copy.size(), 0U);
  BOOST_CHECK(copy.getPtr(9) == NULL);
  BOOST_CHECK_EQUAL(*map.getPtr(9), 12);
}

BOOST_AUTO_TEST_CASE(SbSequentialMap_matches_std_map_with_holes)
{
  SbSequentialMap<int, int> map;
  std::map<int, int> reference;

  uint32_t state = 0x91e10da5U;
  for (int operation = 0; operation < 2000; ++operation) {
    state = state * 1664525U + 1013904223U;
    const int key = static_cast<int>((state >> 8) % 257U);
    if ((state & 7U) < 5U) {
      const int value = operation * 3;
      map.put(key, value);
      reference[key] = value;
    }
    else {
      BOOST_CHECK_EQUAL(map.erase(key), reference.erase(key));
    }

    BOOST_CHECK_EQUAL(map.size(), reference.size());
    const int * found = map.getPtr(key);
    std::map<int, int>::const_iterator expected = reference.find(key);
    BOOST_CHECK_EQUAL(found != NULL, expected != reference.end());
    if (found != NULL) BOOST_CHECK_EQUAL(*found, expected->second);
  }

  SbList<int> keys;
  map.makeKeyList(keys);
  BOOST_REQUIRE_EQUAL(static_cast<size_t>(keys.getLength()), reference.size());
  int index = 0;
  for (std::map<int, int>::const_iterator it = reference.begin();
       it != reference.end(); ++it, ++index) {
    BOOST_CHECK_EQUAL(keys[index], it->first);
  }
}
