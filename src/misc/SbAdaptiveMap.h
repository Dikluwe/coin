#ifndef COIN_SBADAPTIVEMAP_H
#define COIN_SBADAPTIVEMAP_H

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
#include <vector>

#include <Inventor/SbBasic.h>
#include <Inventor/lists/SbList.h>

#include "misc/SbRobinHoodHash.h"

/* A non-owning map for data sets which are usually small but may grow.
   Entries are stored inline and searched linearly through InlineCapacity,
   then spill into a contiguous vector. A lazily allocated flat hash is only
   used after HashThreshold entries. At promotion, the observed hashes choose
   between direct bucket mapping and Fibonacci spreading. clear(), or erasing
   the last entry, returns the map to inline mode while retaining any allocated
   vector and hash storage for inexpensive reuse. releaseStorage() also clears
   the map, but releases both spill tiers.

   Key and Type need not be default-constructible, but must be
   copy-constructible and copy-assignable. No ordering or iterator stability
   is guaranteed. */
template <typename Key, typename Type, unsigned int InlineCapacity,
          unsigned int HashThreshold = 32>
class SbAdaptiveMap {
public:
  static_assert(InlineCapacity > 0,
                "SbAdaptiveMap requires a positive inline capacity");
  static_assert(HashThreshold > InlineCapacity,
                "SbAdaptiveMap hash threshold must exceed inline capacity");

  class Entry {
  public:
    Entry(const Key & keyarg, const Type & objarg)
      : key(keyarg), obj(objarg) { }

    Key key;
    Type obj;
  };

  SbAdaptiveMap(void)
    : numinline(0), overflow(NULL), hashmap(NULL), mode(INLINE_MODE) { }

  SbAdaptiveMap(const SbAdaptiveMap & other)
    : numinline(0), overflow(NULL), hashmap(NULL), mode(INLINE_MODE)
  {
    this->copyFrom(other);
  }

  SbAdaptiveMap & operator=(const SbAdaptiveMap & other)
  {
    if (this != &other) {
      this->clear();
      this->copyFrom(other);
    }
    return *this;
  }

  ~SbAdaptiveMap(void)
  {
    this->releaseStorage();
  }

  SbBool put(const Key & key, const Type & obj)
  {
    if (this->mode == HASH_MODE) return this->hashmap->put(key, obj);

    if (this->mode == VECTOR_MODE) {
      const int index = this->findVector(key);
      if (index >= 0) {
        (*this->overflow)[static_cast<size_t>(index)].obj = obj;
        return FALSE;
      }
      if (this->overflow->size() < HashThreshold) {
        this->overflow->push_back(Entry(key, obj));
        return TRUE;
      }
      return this->promoteVectorAndPut(key, obj);
    }

    const int index = this->findInline(key);
    if (index >= 0) {
      this->inlineEntry(static_cast<unsigned int>(index))->obj = obj;
      return FALSE;
    }

    if (this->numinline < InlineCapacity) {
      new (&this->inlinestorage[this->numinline]) Entry(key, obj);
      ++this->numinline;
      return TRUE;
    }

    return this->spillAndPut(key, obj);
  }

  // Insert without replacing an existing value.
  SbBool insert(const Key & key, const Type & obj)
  {
    if (this->contains(key)) return FALSE;
    return this->put(key, obj);
  }

  SbBool get(const Key & key, Type & obj) const
  {
    if (this->mode == HASH_MODE) return this->hashmap->get(key, obj);
    if (this->mode == VECTOR_MODE) {
      const int index = this->findVector(key);
      if (index < 0) return FALSE;
      obj = (*this->overflow)[static_cast<size_t>(index)].obj;
      return TRUE;
    }
    const int index = this->findInline(key);
    if (index < 0) return FALSE;
    obj = this->inlineEntry(static_cast<unsigned int>(index))->obj;
    return TRUE;
  }

  size_t erase(const Key & key)
  {
    if (this->mode == HASH_MODE) {
      const size_t erased = this->hashmap->erase(key);
      if (erased && this->hashmap->getNumElements() == 0) {
        this->mode = INLINE_MODE;
      }
      return erased;
    }

    if (this->mode == VECTOR_MODE) {
      const int found = this->findVector(key);
      if (found < 0) return 0;
      const size_t index = static_cast<size_t>(found);
      const size_t last = this->overflow->size() - 1;
      if (index != last) (*this->overflow)[index] = (*this->overflow)[last];
      this->overflow->pop_back();
      if (this->overflow->empty()) this->mode = INLINE_MODE;
      return 1;
    }

    const int found = this->findInline(key);
    if (found < 0) return 0;
    const unsigned int index = static_cast<unsigned int>(found);
    const unsigned int last = this->numinline - 1;
    if (index != last) *this->inlineEntry(index) = *this->inlineEntry(last);
    this->inlineEntry(last)->~Entry();
    --this->numinline;
    return 1;
  }

  void clear(void)
  {
    if (this->mode == HASH_MODE) {
      this->hashmap->clear();
    }
    else if (this->mode == VECTOR_MODE) {
      this->overflow->clear();
    }
    else {
      this->destroyInline();
    }
    this->numinline = 0;
    this->mode = INLINE_MODE;
  }

  void releaseStorage(void)
  {
    this->clear();
    delete this->overflow;
    this->overflow = NULL;
    delete this->hashmap;
    this->hashmap = NULL;
  }

  unsigned int getNumElements(void) const
  {
    if (this->mode == HASH_MODE) return this->hashmap->getNumElements();
    if (this->mode == VECTOR_MODE) {
      return static_cast<unsigned int>(this->overflow->size());
    }
    return this->numinline;
  }

  template <typename Visitor>
  void forEach(Visitor visitor) const
  {
    if (this->mode == HASH_MODE) {
      this->hashmap->forEach(visitor);
      return;
    }
    if (this->mode == VECTOR_MODE) {
      for (typename EntryVector::const_iterator it = this->overflow->begin();
           it != this->overflow->end(); ++it) {
        visitor(it->key, it->obj);
      }
      return;
    }
    for (unsigned int i = 0; i < this->numinline; ++i) {
      const Entry * entry = this->inlineEntry(i);
      visitor(entry->key, entry->obj);
    }
  }

  void makeKeyList(SbList<Key> & keys) const
  {
    if (this->mode == HASH_MODE) {
      this->hashmap->makeKeyList(keys);
      return;
    }
    if (this->mode == VECTOR_MODE) {
      for (typename EntryVector::const_iterator it = this->overflow->begin();
           it != this->overflow->end(); ++it) {
        keys.append(it->key);
      }
      return;
    }
    for (unsigned int i = 0; i < this->numinline; ++i) {
      keys.append(this->inlineEntry(i)->key);
    }
  }

private:
  typedef SbRobinHoodHash<Key, Type> HashMap;
  typedef std::vector<Entry> EntryVector;
  typedef typename std::aligned_storage<sizeof(Entry), alignof(Entry)>::type
    EntryStorage;

  enum StorageMode {
    INLINE_MODE,
    VECTOR_MODE,
    HASH_MODE
  };

  Entry * inlineEntry(unsigned int index)
  {
    assert(index < this->numinline);
    return reinterpret_cast<Entry *>(&this->inlinestorage[index]);
  }

  const Entry * inlineEntry(unsigned int index) const
  {
    assert(index < this->numinline);
    return reinterpret_cast<const Entry *>(&this->inlinestorage[index]);
  }

  int findInline(const Key & key) const
  {
    for (unsigned int i = 0; i < this->numinline; ++i) {
      if (this->inlineEntry(i)->key == key) return static_cast<int>(i);
    }
    return -1;
  }

  int findVector(const Key & key) const
  {
    assert(this->overflow);
    for (size_t i = 0; i < this->overflow->size(); ++i) {
      if ((*this->overflow)[i].key == key) return static_cast<int>(i);
    }
    return -1;
  }

  SbBool contains(const Key & key) const
  {
    if (this->mode == HASH_MODE) {
      return this->hashmap->contains(key);
    }
    if (this->mode == VECTOR_MODE) return this->findVector(key) >= 0;
    return this->findInline(key) >= 0;
  }

  void ensureVector(void)
  {
    if (this->overflow == NULL) {
      this->overflow = new EntryVector;
    }
    assert(this->overflow->empty());
  }

  void ensureHash(typename HashMap::HashPolicy hashpolicy)
  {
    if (this->hashmap == NULL) {
      this->hashmap = new HashMap(HashThreshold + 1, hashpolicy);
    }
    else this->hashmap->setHashPolicy(hashpolicy);
    assert(this->hashmap->getNumElements() == 0);
  }

  typename HashMap::HashPolicy chooseHashPolicy(const Key & incoming) const
  {
    uint32_t hashes[HashThreshold + 1];
    unsigned int count = 0;
    for (typename EntryVector::const_iterator it = this->overflow->begin();
         it != this->overflow->end(); ++it) {
      hashes[count++] = static_cast<uint32_t>(SbHashFunc(it->key));
    }
    hashes[count++] = static_cast<uint32_t>(SbHashFunc(incoming));
    return HashMap::chooseHashPolicy(hashes, count, HashThreshold + 1);
  }

  SbBool spillAndPut(const Key & key, const Type & obj)
  {
    this->ensureVector();
    this->overflow->reserve(InlineCapacity + 1);
    for (unsigned int i = 0; i < this->numinline; ++i) {
      const Entry * entry = this->inlineEntry(i);
      this->overflow->push_back(*entry);
    }
    this->destroyInline();
    this->numinline = 0;
    this->overflow->push_back(Entry(key, obj));
    this->mode = VECTOR_MODE;
    return TRUE;
  }

  SbBool promoteVectorAndPut(const Key & key, const Type & obj)
  {
    this->ensureHash(this->chooseHashPolicy(key));
    for (typename EntryVector::const_iterator it = this->overflow->begin();
         it != this->overflow->end(); ++it) {
      this->hashmap->put(it->key, it->obj);
    }
    const SbBool inserted = this->hashmap->put(key, obj);
    this->overflow->clear();
    this->mode = HASH_MODE;
    return inserted;
  }

  void destroyInline(void)
  {
    for (unsigned int i = 0; i < this->numinline; ++i) {
      this->inlineEntry(i)->~Entry();
    }
  }

  void copyFrom(const SbAdaptiveMap & other)
  {
    if (other.mode == HASH_MODE) {
      other.hashmap->forEach([this](const Key & key, const Type & obj) {
        this->put(key, obj);
      });
    }
    else if (other.mode == VECTOR_MODE) {
      for (typename EntryVector::const_iterator it = other.overflow->begin();
           it != other.overflow->end(); ++it) {
        this->put(it->key, it->obj);
      }
    }
    else {
      for (unsigned int i = 0; i < other.numinline; ++i) {
        const Entry * entry = other.inlineEntry(i);
        this->put(entry->key, entry->obj);
      }
    }
  }

  EntryStorage inlinestorage[InlineCapacity];
  unsigned int numinline;
  EntryVector * overflow;
  HashMap * hashmap;
  StorageMode mode;
};

#endif // !COIN_SBADAPTIVEMAP_H
