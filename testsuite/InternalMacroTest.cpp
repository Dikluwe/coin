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

/**
 * Prove that internal tests can include private implementation headers while
 * retaining the ABI role of ordinary Coin consumers.
 */
#ifndef COIN_INTERNAL
#error InternalMacroTest must have access to private Coin interfaces.
#endif

#ifdef COIN_BUILDING_COIN
#error InternalMacroTest is not part of the Coin library.
#endif

#include "misc/SbHash.h"
#include "CoinTest.h"

static_assert(sizeof(SbHash<int, int>) > 0, "private Coin type is unavailable");

namespace {

class SbHashCopyCounter {
public:
  SbHashCopyCounter(int valuearg = 0) : value(valuearg) { }
  SbHashCopyCounter(const SbHashCopyCounter & other) : value(other.value) {
    ++copies;
  }
  ~SbHashCopyCounter() { ++destructions; }

  SbHashCopyCounter & operator=(const SbHashCopyCounter & other) {
    this->value = other.value;
    return *this;
  }

  static int copies;
  static int destructions;
  int value;
};

int SbHashCopyCounter::copies = 0;
int SbHashCopyCounter::destructions = 0;

class SbHashIndexProbe : public SbHash<unsigned int, int> {
public:
  SbHashIndexProbe(unsigned int sizearg) : SbHash<unsigned int, int>(sizearg) { }

  unsigned int bucketIndex(unsigned int key) const {
    return this->getIndex(key);
  }

  SbBool storageIsAllocated(void) const {
    return this->hasAllocatedStorage();
  }
};

} // namespace

BOOST_AUTO_TEST_CASE(SbHash_preserves_legacy_bucket_mapping)
{
  SbHashIndexProbe hash(257);
  BOOST_CHECK_EQUAL(hash.bucketIndex(0U), 0U);
  BOOST_CHECK_EQUAL(hash.bucketIndex(1U), 1U);
  BOOST_CHECK_EQUAL(hash.bucketIndex(2U), 2U);
  BOOST_CHECK_EQUAL(hash.bucketIndex(3U), 3U);
  BOOST_CHECK_EQUAL(hash.bucketIndex(255U), 255U);
  BOOST_CHECK_EQUAL(hash.bucketIndex(258U), 1U);

  for (unsigned int i = 0; i < 1024; ++i) {
    BOOST_REQUIRE(hash.put(i, static_cast<int>(i + 7)));
  }
  for (unsigned int i = 0; i < 1024; ++i) {
    int value = 0;
    BOOST_REQUIRE(hash.get(i, value));
    BOOST_CHECK_EQUAL(value, static_cast<int>(i + 7));
  }
}

BOOST_AUTO_TEST_CASE(SbHash_resize_relinks_entries)
{
  SbHash<int, SbHashCopyCounter> hash(3);
  SbHashCopyCounter one(1);
  SbHashCopyCounter two(2);
  SbHashCopyCounter three(3);

  BOOST_REQUIRE(hash.put(1, one));
  BOOST_REQUIRE(hash.put(2, two));

  SbHash<int, SbHashCopyCounter>::const_iterator before = hash.find(1);
  BOOST_REQUIRE(before != hash.const_end());
  const SbHashCopyCounter * address = &before->obj;

  SbHashCopyCounter::copies = 0;
  SbHashCopyCounter::destructions = 0;

  // With three buckets and the default 0.75 load factor, this insertion
  // triggers a resize. Only the newly inserted value should be copied.
  BOOST_REQUIRE(hash.put(3, three));
  BOOST_CHECK_EQUAL(SbHashCopyCounter::copies, 1);
  BOOST_CHECK_EQUAL(SbHashCopyCounter::destructions, 0);

  SbHash<int, SbHashCopyCounter>::const_iterator after = hash.find(1);
  BOOST_REQUIRE(after != hash.const_end());
  BOOST_CHECK_EQUAL(&after->obj, address);
  BOOST_CHECK_EQUAL(after->obj.value, 1);
  BOOST_CHECK_EQUAL(hash.getNumElements(), 3U);
}

BOOST_AUTO_TEST_CASE(SbHash_allocates_lazily_and_can_release_storage)
{
  SbHashIndexProbe hash(257);
  BOOST_CHECK(!hash.storageIsAllocated());

  int value = -1;
  BOOST_CHECK(!hash.get(1U, value));
  BOOST_CHECK_EQUAL(hash.erase(1U), static_cast<size_t>(0));
  BOOST_CHECK(hash.const_begin() == hash.const_end());
  BOOST_CHECK(!hash.storageIsAllocated());

  BOOST_REQUIRE(hash.put(1U, 10));
  BOOST_CHECK(hash.storageIsAllocated());
  hash.clear();
  BOOST_CHECK(hash.storageIsAllocated());
  BOOST_CHECK_EQUAL(hash.getNumElements(), 0U);

  hash.releaseStorage();
  BOOST_CHECK(!hash.storageIsAllocated());
  BOOST_REQUIRE(hash.put(2U, 20));
  BOOST_REQUIRE(hash.get(2U, value));
  BOOST_CHECK_EQUAL(value, 20);
}

BOOST_AUTO_TEST_CASE(SbHash_iterates_and_handles_self_assignment)
{
  SbHash<unsigned int, int> hash(3);
  hash.put(1U, 10);
  hash.put(2U, 20);
  hash.put(3U, 30);

  int sum = 0;
  unsigned int count = 0;
  for (SbHash<unsigned int, int>::iterator it = hash.begin();
       it != hash.end(); ++it) {
    sum += it->obj;
    ++count;
  }
  BOOST_CHECK_EQUAL(count, 3U);
  BOOST_CHECK_EQUAL(sum, 60);

  hash = hash;
  BOOST_CHECK_EQUAL(hash.getNumElements(), 3U);
  BOOST_REQUIRE(hash.get(3U, sum));
  BOOST_CHECK_EQUAL(sum, 30);
}
