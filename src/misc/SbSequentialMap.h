#ifndef COIN_SBSEQUENTIALMAP_H
#define COIN_SBSEQUENTIALMAP_H

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

#ifndef COIN_INTERNAL
#error this is a private header file
#endif /* !COIN_INTERNAL */

#include <assert.h>
#include <stddef.h>
#include <type_traits>
#include <vector>

#include <Inventor/SbBasic.h>
#include <Inventor/lists/SbList.h>

#include "coindefs.h"

/* An ordered map for non-negative integer keys which chooses its layout from
   the observed key distribution. Small or sparse sets use a sorted contiguous
   vector. At DenseThreshold entries, a range with at most MaxSlack slots per
   entry is promoted to direct indexing with a byte occupancy array.

   Both layouts iterate in ascending key order. This makes the container a
   candidate for replacing std::map when ordering is part of the behavior,
   without paying for a full key range when a scene uses only a sparse subset
   of a globally sequential ID space. Type must be default-constructible and
   copy-assignable. */
template <typename Key, typename Type,
          unsigned int DenseThreshold = 8, unsigned int MaxSlack = 2>
class SbSequentialMap {
public:
  static_assert(std::is_integral<Key>::value,
                "SbSequentialMap keys must be integral");
  static_assert(DenseThreshold > 1,
                "SbSequentialMap dense threshold must exceed one");
  static_assert(MaxSlack > 0,
                "SbSequentialMap maximum slack must be positive");

  class Entry {
  public:
    Entry(const Key keyarg, const Type & objarg)
      : key(keyarg), obj(objarg) { }

    Key key;
    Type obj;
  };

  SbSequentialMap(void)
    : numelements(0), densebase(0), densemode(FALSE) { }

  SbBool put(const Key key, const Type & obj)
  {
    this->assertValidKey(key);
    if (this->densemode) return this->putDense(key, obj);

    const size_t position = this->lowerBound(key);
    if (position < this->sparse.size() && this->sparse[position].key == key) {
      this->sparse[position].obj = obj;
      return FALSE;
    }
    this->sparse.insert(this->sparse.begin() + position, Entry(key, obj));
    ++this->numelements;
    this->promoteIfDense();
    return TRUE;
  }

  Type & operator[](const Key key)
  {
    this->assertValidKey(key);
    Type * existing = this->getPtr(key);
    if (existing != NULL) return *existing;
    this->put(key, Type());
    existing = this->getPtr(key);
    assert(existing != NULL);
    return *existing;
  }

  SbBool get(const Key key, Type & obj) const
  {
    const Type * value = this->getPtr(key);
    if (value == NULL) return FALSE;
    obj = *value;
    return TRUE;
  }

  Type * getPtr(const Key key)
  {
    return const_cast<Type *>(static_cast<const SbSequentialMap *>(this)->getPtr(key));
  }

  const Type * getPtr(const Key key) const
  {
    if (this->isNegative(key)) return NULL;
    if (this->densemode) {
      const size_t index = static_cast<size_t>(key);
      if (index < this->densebase) return NULL;
      const size_t offset = index - this->densebase;
      if (offset >= this->occupied.size() || !this->occupied[offset]) return NULL;
      return &this->densevalues[offset];
    }

    const size_t position = this->lowerBound(key);
    if (position == this->sparse.size() || this->sparse[position].key != key) {
      return NULL;
    }
    return &this->sparse[position].obj;
  }

  size_t erase(const Key key)
  {
    if (this->isNegative(key)) return 0;
    if (!this->densemode) {
      const size_t position = this->lowerBound(key);
      if (position == this->sparse.size() || this->sparse[position].key != key) {
        return 0;
      }
      this->sparse.erase(this->sparse.begin() + position);
      --this->numelements;
      return 1;
    }

    const size_t index = static_cast<size_t>(key);
    if (index < this->densebase) return 0;
    const size_t offset = index - this->densebase;
    if (offset >= this->occupied.size() || !this->occupied[offset]) return 0;
    this->densevalues[offset] = Type();
    this->occupied[offset] = 0;
    --this->numelements;
    if (this->numelements < DenseThreshold ||
        this->occupied.size() > this->numelements * MaxSlack * 2U) {
      this->demoteToSparse();
    }
    return 1;
  }

  void clear(void)
  {
    this->sparse.clear();
    this->densevalues.clear();
    this->occupied.clear();
    this->numelements = 0;
    this->densebase = 0;
    this->densemode = FALSE;
  }

  unsigned int getNumElements(void) const
  {
    return this->numelements;
  }

  size_t size(void) const
  {
    return static_cast<size_t>(this->numelements);
  }

  SbBool isDenseMode(void) const
  {
    return this->densemode;
  }

  void makeKeyList(SbList<Key> & keys) const
  {
    if (!this->densemode) {
      for (size_t i = 0; i < this->sparse.size(); ++i) {
        keys.append(this->sparse[i].key);
      }
      return;
    }
    for (size_t i = 0; i < this->occupied.size(); ++i) {
      if (this->occupied[i]) {
        keys.append(static_cast<Key>(this->densebase + i));
      }
    }
  }

  size_t getAllocatedBytes(void) const
  {
    return this->sparse.capacity() * sizeof(Entry) +
      this->densevalues.capacity() * sizeof(Type) +
      this->occupied.capacity() * sizeof(unsigned char);
  }

private:
  static SbBool isNegative(const Key key)
  {
    return std::is_signed<Key>::value && key < static_cast<Key>(0);
  }

  static void assertValidKey(const Key key)
  {
    assert(!SbSequentialMap::isNegative(key));
  }

  size_t lowerBound(const Key key) const
  {
    if (this->sparse.size() <= DenseThreshold) {
      size_t position = 0;
      while (position < this->sparse.size() &&
             this->sparse[position].key < key) {
        ++position;
      }
      return position;
    }

    size_t first = 0;
    size_t count = this->sparse.size();
    while (count > 0) {
      const size_t step = count / 2;
      const size_t middle = first + step;
      if (this->sparse[middle].key < key) {
        first = middle + 1;
        count -= step + 1;
      }
      else {
        count = step;
      }
    }
    return first;
  }

  void promoteIfDense(void)
  {
    if (this->sparse.size() < DenseThreshold) return;
    const size_t first = static_cast<size_t>(this->sparse.front().key);
    const size_t last = static_cast<size_t>(this->sparse.back().key);
    const size_t span = last - first + 1;
    if (span > this->sparse.size() * MaxSlack) return;

    this->densebase = first;
    this->densevalues.assign(span, Type());
    this->occupied.assign(span, 0);
    for (size_t i = 0; i < this->sparse.size(); ++i) {
      const size_t offset = static_cast<size_t>(this->sparse[i].key) - first;
      this->densevalues[offset] = this->sparse[i].obj;
      this->occupied[offset] = 1;
    }
    this->sparse.clear();
    this->densemode = TRUE;
  }

  SbBool putDense(const Key key, const Type & obj)
  {
    const size_t index = static_cast<size_t>(key);
    const size_t oldend = this->densebase + this->occupied.size();
    if (index < this->densebase || index >= oldend) {
      const size_t newbase = index < this->densebase ? index : this->densebase;
      const size_t newend = index >= oldend ? index + 1 : oldend;
      const size_t newspan = newend - newbase;
      if (newspan > (this->numelements + 1U) * MaxSlack * 2U) {
        this->demoteToSparse();
        return this->put(key, obj);
      }
      this->expandDense(newbase, newspan);
    }

    const size_t offset = index - this->densebase;
    const SbBool inserted = this->occupied[offset] == 0;
    this->densevalues[offset] = obj;
    if (inserted) {
      this->occupied[offset] = 1;
      ++this->numelements;
    }
    return inserted;
  }

  void expandDense(const size_t newbase, const size_t newspan)
  {
    std::vector<Type> newvalues(newspan);
    std::vector<unsigned char> newoccupied(newspan, 0);
    const size_t shift = this->densebase - newbase;
    for (size_t i = 0; i < this->occupied.size(); ++i) {
      if (this->occupied[i]) {
        newvalues[i + shift] = this->densevalues[i];
        newoccupied[i + shift] = 1;
      }
    }
    this->densevalues.swap(newvalues);
    this->occupied.swap(newoccupied);
    this->densebase = newbase;
  }

  void demoteToSparse(void)
  {
    this->sparse.clear();
    this->sparse.reserve(this->numelements);
    for (size_t i = 0; i < this->occupied.size(); ++i) {
      if (this->occupied[i]) {
        this->sparse.push_back(Entry(static_cast<Key>(this->densebase + i),
                                     this->densevalues[i]));
      }
    }
    this->densevalues.clear();
    this->occupied.clear();
    this->densebase = 0;
    this->densemode = FALSE;
  }

  std::vector<Entry> sparse;
  std::vector<Type> densevalues;
  std::vector<unsigned char> occupied;
  unsigned int numelements;
  size_t densebase;
  SbBool densemode;
};

#endif // !COIN_SBSEQUENTIALMAP_H
