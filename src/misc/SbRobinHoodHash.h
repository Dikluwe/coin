#ifndef COIN_SBROBINHOODHASH_H
#define COIN_SBROBINHOODHASH_H

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
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include <Inventor/SbBasic.h>
#include <Inventor/lists/SbList.h>

#include "misc/SbHash.h"

/* Contiguous open-addressed hash table using Robin Hood displacement.
   Entries and probe metadata use separate dense arrays so alignment padding
   is not repeated in every slot. A zero probe distance marks an empty slot;
   occupied slots store their distance from the ideal bucket plus one. The
   table retains its allocation across clear() and never exceeds a 75 percent
   load factor. Callers may select direct bucket mapping for already uniform
   hashes or Fibonacci spreading for patterned hashes. Direct mapping changes
   to spreading once if it encounters a long probe.

   Key and Type need not be default-constructible, but must be
   copy-constructible and copy-assignable. */
template <typename Key, typename Type>
class SbRobinHoodHash {
public:
  enum HashPolicy {
    DIRECT_HASH,
    SPREAD_HASH
  };

  class Entry {
  public:
    Entry(const Key & keyarg, const Type & objarg)
      : key(keyarg), obj(objarg) { }

    Key key;
    Type obj;
  };

  class const_iterator;

  class iterator {
  public:
    iterator(void) : master(NULL), index(0) { }
    Entry & operator*(void) const { return *this->master->entryAt(this->index); }
    Entry * operator->(void) const { return this->master->entryAt(this->index); }
    bool operator==(const iterator & other) const {
      return this->master == other.master && this->index == other.index;
    }
    bool operator!=(const iterator & other) const { return !(*this == other); }
    iterator & operator++(void) {
      ++this->index;
      this->skipEmpty();
      return *this;
    }
  private:
    iterator(SbRobinHoodHash * map, size_t position)
      : master(map), index(position) { this->skipEmpty(); }
    void skipEmpty(void) {
      if (this->master == NULL) return;
      while (this->index < this->master->capacity &&
             this->master->distances[this->index] == 0) ++this->index;
    }
    SbRobinHoodHash * master;
    size_t index;
    friend class SbRobinHoodHash<Key, Type>;
    friend class const_iterator;
  };

  class const_iterator {
  public:
    const_iterator(void) : master(NULL), index(0) { }
    const_iterator(const iterator & other)
      : master(other.master), index(other.index) { }
    const Entry & operator*(void) const { return *this->master->entryAt(this->index); }
    const Entry * operator->(void) const { return this->master->entryAt(this->index); }
    bool operator==(const const_iterator & other) const {
      return this->master == other.master && this->index == other.index;
    }
    bool operator!=(const const_iterator & other) const { return !(*this == other); }
    const_iterator & operator++(void) {
      ++this->index;
      this->skipEmpty();
      return *this;
    }
  private:
    const_iterator(const SbRobinHoodHash * map, size_t position)
      : master(map), index(position) { this->skipEmpty(); }
    void skipEmpty(void) {
      if (this->master == NULL) return;
      while (this->index < this->master->capacity &&
             this->master->distances[this->index] == 0) ++this->index;
    }
    const SbRobinHoodHash * master;
    size_t index;
    friend class SbRobinHoodHash<Key, Type>;
  };

  explicit SbRobinHoodHash(unsigned int expectedelements = 0,
                           HashPolicy policyarg = DIRECT_HASH)
    : entries(NULL), distances(NULL), capacity(0), elements(0), hashshift(0),
      hashpolicy(policyarg)
  {
    if (expectedelements != 0) {
      this->allocate(this->capacityForElements(expectedelements));
    }
  }

  SbRobinHoodHash(const SbRobinHoodHash & other)
    : entries(NULL), distances(NULL), capacity(0), elements(0), hashshift(0),
      hashpolicy(other.hashpolicy)
  {
    if (other.capacity != 0) this->allocate(other.capacity);
    other.forEach([this](const Key & key, const Type & obj) {
      this->put(key, obj);
    });
  }

  SbRobinHoodHash & operator=(const SbRobinHoodHash & other)
  {
    if (this != &other) {
      this->clear();
      this->hashpolicy = other.hashpolicy;
      if (this->capacity < other.capacity) this->rehash(other.capacity);
      other.forEach([this](const Key & key, const Type & obj) {
        this->put(key, obj);
      });
    }
    return *this;
  }

  ~SbRobinHoodHash(void)
  {
    this->releaseStorage();
  }

  iterator begin(void) { return iterator(this, 0); }
  iterator end(void) { return iterator(this, this->capacity); }
  const_iterator const_begin(void) const { return const_iterator(this, 0); }
  const_iterator const_end(void) const {
    return const_iterator(this, this->capacity);
  }

  const_iterator find(const Key & key) const
  {
    const size_t found = this->findIndex(key);
    return found == NOT_FOUND ? this->const_end() : const_iterator(this, found);
  }

  Type & operator[](const Key & key)
  {
    size_t found = this->findIndex(key);
    if (found == NOT_FOUND) {
      this->put(key, Type());
      found = this->findIndex(key);
      assert(found != NOT_FOUND);
    }
    return this->entryAt(found)->obj;
  }

  SbBool put(const Key & key, const Type & obj)
  {
    const size_t found = this->findIndex(key);
    if (found != NOT_FOUND) {
      this->entryAt(found)->obj = obj;
      return FALSE;
    }

    this->ensureInsertCapacity();
    if (this->hashpolicy == DIRECT_HASH &&
        this->insertionProbeLength(key) > MAX_DIRECT_PROBE) {
      this->hashpolicy = SPREAD_HASH;
      this->rehash(this->capacity);
    }
    this->insertNew(key, obj);
    return TRUE;
  }

  SbBool get(const Key & key, Type & obj) const
  {
    const size_t found = this->findIndex(key);
    if (found == NOT_FOUND) return FALSE;
    obj = this->entryAt(found)->obj;
    return TRUE;
  }

  SbBool contains(const Key & key) const
  {
    return this->findIndex(key) != NOT_FOUND;
  }

  size_t erase(const Key & key)
  {
    size_t index = this->findIndex(key);
    if (index == NOT_FOUND) return 0;

    this->entryAt(index)->~Entry();
    this->distances[index] = 0;

    size_t next = (index + 1) & (this->capacity - 1);
    while (this->distances[next] > 1) {
      new (&this->entries[index]) Entry(*this->entryAt(next));
      this->distances[index] = this->distances[next] - 1;
      this->entryAt(next)->~Entry();
      this->distances[next] = 0;
      index = next;
      next = (next + 1) & (this->capacity - 1);
    }
    --this->elements;
    return 1;
  }

  void clear(void)
  {
    for (size_t i = 0; i < this->capacity; ++i) {
      if (this->distances[i] != 0) {
        this->entryAt(i)->~Entry();
        this->distances[i] = 0;
      }
    }
    this->elements = 0;
  }

  void releaseStorage(void)
  {
    this->clear();
    delete [] this->entries;
    delete [] this->distances;
    this->entries = NULL;
    this->distances = NULL;
    this->capacity = 0;
  }

  void reserve(unsigned int expectedelements)
  {
    if (expectedelements == 0) return;
    const size_t wanted = this->capacityForElements(expectedelements);
    if (wanted > this->capacity) this->rehash(wanted);
  }

  void setHashPolicy(HashPolicy policyarg)
  {
    if (this->hashpolicy == policyarg) return;
    this->hashpolicy = policyarg;
    if (this->elements != 0) this->rehash(this->capacity);
  }

  HashPolicy getHashPolicy(void) const { return this->hashpolicy; }

  static size_t capacityForExpectedElements(unsigned int count)
  {
    size_t result = 8;
    while (static_cast<size_t>(count) * 4 > result * 3) result *= 2;
    return result;
  }

  static HashPolicy chooseHashPolicy(const uint32_t * hashes,
                                     unsigned int count,
                                     unsigned int expectedelements)
  {
    const size_t tablecapacity = capacityForExpectedElements(expectedelements);
    const unsigned int shift = hashShiftForCapacity(tablecapacity);
    size_t directcollisions = 0;
    size_t spreadcollisions = 0;
    if (tablecapacity <= 64) {
      uint64_t directseen = 0;
      uint64_t spreadseen = 0;
      for (unsigned int i = 0; i < count; ++i) {
        const uint64_t directbit = UINT64_C(1) <<
          (static_cast<size_t>(hashes[i]) & (tablecapacity - 1));
        const uint64_t spreadbit = UINT64_C(1) <<
          static_cast<size_t>(spreadHash(hashes[i]) >> shift);
        if ((directseen & directbit) != 0) ++directcollisions;
        if ((spreadseen & spreadbit) != 0) ++spreadcollisions;
        directseen |= directbit;
        spreadseen |= spreadbit;
      }
      return directcollisions <= spreadcollisions ? DIRECT_HASH : SPREAD_HASH;
    }
    for (unsigned int i = 0; i < count; ++i) {
      const size_t directbucket = static_cast<size_t>(hashes[i]) &
        (tablecapacity - 1);
      const size_t spreadbucket = static_cast<size_t>(spreadHash(hashes[i]) >> shift);
      for (unsigned int j = 0; j < i; ++j) {
        if ((static_cast<size_t>(hashes[j]) & (tablecapacity - 1)) ==
            directbucket) ++directcollisions;
        if (static_cast<size_t>(spreadHash(hashes[j]) >> shift) ==
            spreadbucket) ++spreadcollisions;
      }
    }
    return directcollisions <= spreadcollisions ? DIRECT_HASH : SPREAD_HASH;
  }

  unsigned int getNumElements(void) const
  {
    return static_cast<unsigned int>(this->elements);
  }

  template <typename Visitor>
  void forEach(Visitor visitor) const
  {
    for (size_t i = 0; i < this->capacity; ++i) {
      if (this->distances[i] != 0) {
        const Entry * entry = this->entryAt(i);
        visitor(entry->key, entry->obj);
      }
    }
  }

  void makeKeyList(SbList<Key> & keys) const
  {
    for (size_t i = 0; i < this->capacity; ++i) {
      if (this->distances[i] != 0) {
        keys.append(this->entryAt(i)->key);
      }
    }
  }

private:
  typedef typename std::aligned_storage<sizeof(Entry), alignof(Entry)>::type
    EntryStorage;

  static const size_t NOT_FOUND = static_cast<size_t>(-1);
  static const size_t MAX_DIRECT_PROBE = 8;

  static uint32_t spreadHash(uint32_t hash)
  {
    /* Fibonacci hashing spreads sequential integers and aligned pointers over
       the high bits needed by a power-of-two table with one multiplication. */
    return hash * UINT32_C(0x9e3779b1);
  }

  size_t capacityForElements(size_t count) const
  {
    assert(count <= static_cast<size_t>(UINT32_MAX));
    return capacityForExpectedElements(static_cast<unsigned int>(count));
  }

  void allocate(size_t newcapacity)
  {
    assert(this->entries == NULL);
    assert(this->distances == NULL);
    assert((newcapacity & (newcapacity - 1)) == 0);
    this->entries = new EntryStorage[newcapacity];
    this->distances = new uint32_t[newcapacity]();
    this->capacity = newcapacity;
    this->hashshift = this->hashShiftForCapacity(newcapacity);
  }

  Entry * entryAt(size_t index)
  {
    return reinterpret_cast<Entry *>(&this->entries[index]);
  }

  const Entry * entryAt(size_t index) const
  {
    return reinterpret_cast<const Entry *>(&this->entries[index]);
  }

  size_t idealIndex(const Key & key) const
  {
    const uint32_t hash = SbHashFunc(key);
    if (this->hashpolicy == DIRECT_HASH) {
      return static_cast<size_t>(hash) & (this->capacity - 1);
    }
    return static_cast<size_t>(spreadHash(hash) >> this->hashshift);
  }

  size_t insertionProbeLength(const Key & key) const
  {
    size_t index = this->idealIndex(key);
    size_t distance = 1;
    while (this->distances[index] != 0) {
      index = (index + 1) & (this->capacity - 1);
      ++distance;
    }
    return distance;
  }

  size_t findIndex(const Key & key) const
  {
    if (this->capacity == 0) return NOT_FOUND;
    size_t index = this->idealIndex(key);
    uint32_t distance = 1;
    while (this->distances[index] >= distance) {
      if (this->distances[index] == distance &&
          this->entryAt(index)->key == key) {
        return index;
      }
      index = (index + 1) & (this->capacity - 1);
      ++distance;
    }
    return NOT_FOUND;
  }

  void ensureInsertCapacity(void)
  {
    if (this->capacity == 0) {
      this->allocate(8);
    }
    else if ((this->elements + 1) * 4 > this->capacity * 3) {
      this->rehash(this->capacity * 2);
    }
  }

  void insertNew(const Key & key, const Type & obj)
  {
    Entry incoming(key, obj);
    size_t index = this->idealIndex(key);
    uint32_t distance = 1;

    while (this->distances[index] != 0) {
      if (this->distances[index] < distance) {
        Entry displaced(*this->entryAt(index));
        *this->entryAt(index) = incoming;
        incoming = displaced;
        const uint32_t displacedDistance = this->distances[index];
        this->distances[index] = distance;
        distance = displacedDistance;
      }
      index = (index + 1) & (this->capacity - 1);
      ++distance;
    }

    new (&this->entries[index]) Entry(incoming);
    this->distances[index] = distance;
    ++this->elements;
  }

  void rehash(size_t newcapacity)
  {
    EntryStorage * oldentries = this->entries;
    uint32_t * olddistances = this->distances;
    const size_t oldcapacity = this->capacity;
    this->entries = new EntryStorage[newcapacity];
    this->distances = new uint32_t[newcapacity]();
    this->capacity = newcapacity;
    this->hashshift = this->hashShiftForCapacity(newcapacity);
    this->elements = 0;

    for (size_t i = 0; i < oldcapacity; ++i) {
      if (olddistances[i] != 0) {
        const Entry * entry = reinterpret_cast<const Entry *>(&oldentries[i]);
        this->insertNew(entry->key, entry->obj);
        const_cast<Entry *>(entry)->~Entry();
      }
    }
    delete [] oldentries;
    delete [] olddistances;
  }

  EntryStorage * entries;
  uint32_t * distances;
  size_t capacity;
  size_t elements;
  unsigned int hashshift;
  HashPolicy hashpolicy;

  static unsigned int hashShiftForCapacity(size_t tablecapacity)
  {
    unsigned int bits = 0;
    while (tablecapacity > 1) {
      tablecapacity >>= 1;
      ++bits;
    }
    assert(bits <= 32);
    return 32U - bits;
  }
};

#endif // !COIN_SBROBINHOODHASH_H
