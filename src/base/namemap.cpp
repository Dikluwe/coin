/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
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

#include "base/namemap.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "threads/threadsutilp.h"
#include "tidbitsp.h"
#include "coindefs.h"

#ifndef COIN_WORKAROUND_NO_USING_STD_FUNCS
using std::malloc;
using std::free;
using std::strlen;
using std::strcmp;
using std::memcpy;
using std::memset;
#endif // !COIN_WORKAROUND_NO_USING_STD_FUNCS

/* ************************************************************************* */

/*
  Implementation note: we cannot use the cc_dict ADT to simplify this
  implementation, as cc_dict requires all keys in the hash to be
  unique. That is not necessarily true for a set of strings, as two
  strings can map to the same key (even though the probability is low
  (with a good hash routine)).

  So it is indeed necessary to use our own string hash, where equality
  is tested for with first hash value and then string compare.

  mortene.
*/

/* ************************************************************************* */

static const size_t NAME_CHUNK_CAPACITY = 4096 - 32;
// Keep the original table's small initial footprint, but let independent
// shards grow when applications intern large or concurrent name sets.
static const unsigned int NAME_SHARD_COUNT = 32;
static const unsigned int NAME_INITIAL_BUCKET_COUNT = 64;
static const unsigned int NAME_MAX_AVERAGE_CHAIN = 8;
// This covers the common vocabulary of a traversal without making every
// application thread carry a large secondary name table.
static const unsigned int NAME_LOCAL_CACHE_SIZE = 256;

struct NamemapMemChunk {
  char * curbyte;
  size_t bytesleft;
  struct NamemapMemChunk * next;
};

struct NamemapBucketEntry {
  unsigned long hashvalue;
  const char * str;
  struct NamemapBucketEntry * next;
};

struct NamemapShard {
  void * mutex;
  struct NamemapBucketEntry ** buckets;
  unsigned int bucketcount;
  unsigned int entries;
  struct NamemapMemChunk * headchunk;
};

struct NamemapLocalCacheEntry {
  unsigned long hashvalue;
  const char * str;
};

struct NamemapLocalCache {
  unsigned int generation;
  struct NamemapLocalCacheEntry entries[NAME_LOCAL_CACHE_SIZE];
};

static std::mutex namemap_init_mutex;
static std::atomic<bool> namemap_initialized(false);
static std::atomic<unsigned int> namemap_generation(1);
static struct NamemapShard nameshards[NAME_SHARD_COUNT];
static thread_local struct NamemapLocalCache namemap_local_cache = {};

static unsigned int
namemap_mix_hash(unsigned int hash)
{
  // cc_string_hash_text() predates power-of-two tables. Avalanche its output
  // before selecting a shard and bucket so nearby names do not cluster.
  hash = ((hash >> 16) ^ hash) * 0x045d9f3bU;
  hash ^= hash >> 16;
  return hash;
}

/* ************************************************************************* */

extern "C" {

/* Deallocates static process resources. */
static void
namemap_cleanup(void)
{
  namemap_generation.fetch_add(1, std::memory_order_release);
  for (unsigned int shardidx = 0; shardidx < NAME_SHARD_COUNT; ++shardidx) {
    struct NamemapShard & shard = nameshards[shardidx];
    struct NamemapMemChunk * chunkptr = shard.headchunk;
    while (chunkptr) {
      struct NamemapMemChunk * next = chunkptr->next;
      free(chunkptr);
      chunkptr = next;
    }
    shard.headchunk = NULL;

    for (unsigned int bucket = 0; bucket < shard.bucketcount; ++bucket) {
      struct NamemapBucketEntry * entry = shard.buckets[bucket];
      while (entry) {
        struct NamemapBucketEntry * next = entry->next;
        free(entry);
        entry = next;
      }
    }
    free(shard.buckets);
    shard.buckets = NULL;
    shard.bucketcount = 0;
    shard.entries = 0;
    CC_MUTEX_DESTRUCT(shard.mutex);
  }

  namemap_initialized.store(false, std::memory_order_release);
}

} // extern "C"

/* Initializes static data. */
static void
namemap_init(void)
{
  for (unsigned int i = 0; i < NAME_SHARD_COUNT; ++i) {
    struct NamemapShard & shard = nameshards[i];
    CC_MUTEX_CONSTRUCT(shard.mutex);
    shard.buckets = static_cast<struct NamemapBucketEntry **>(
      malloc(sizeof(struct NamemapBucketEntry *) * NAME_INITIAL_BUCKET_COUNT));
    (void)memset(shard.buckets, 0,
                 sizeof(struct NamemapBucketEntry *) * NAME_INITIAL_BUCKET_COUNT);
    shard.bucketcount = NAME_INITIAL_BUCKET_COUNT;
    shard.entries = 0;
    shard.headchunk = NULL;
  }

  coin_atexit(static_cast<coin_atexit_f *>(namemap_cleanup), CC_ATEXIT_SBNAME);
}

static const char *
find_string_address(struct NamemapShard & shard, const char * s)
{
  const size_t len = strlen(s) + 1;
  if (shard.headchunk == NULL || shard.headchunk->bytesleft < len) {
    const size_t capacity = len > NAME_CHUNK_CAPACITY
      ? len : NAME_CHUNK_CAPACITY;
    struct NamemapMemChunk * newchunk = static_cast<struct NamemapMemChunk *>(
      malloc(sizeof(struct NamemapMemChunk) + capacity)
      );

    newchunk->curbyte = reinterpret_cast<char *>(newchunk + 1);
    newchunk->bytesleft = capacity;
    newchunk->next = shard.headchunk;

    shard.headchunk = newchunk;
  }

  (void)memcpy(shard.headchunk->curbyte, s, len);
  s = shard.headchunk->curbyte;

  shard.headchunk->curbyte += len;
  shard.headchunk->bytesleft -= len;

  return s;
}

static void
namemap_ensure_initialized(void)
{
  if (namemap_initialized.load(std::memory_order_acquire)) return;

  std::lock_guard<std::mutex> guard(namemap_init_mutex);
  if (!namemap_initialized.load(std::memory_order_relaxed)) {
    namemap_init();
    namemap_initialized.store(true, std::memory_order_release);
  }
}

static unsigned int
namemap_bucket_index(unsigned int mixedhash, unsigned int bucketcount)
{
  return (mixedhash / NAME_SHARD_COUNT) & (bucketcount - 1);
}

static const char *
namemap_cache_find(const char * str, unsigned long hashvalue,
                   unsigned int mixedhash)
{
  const unsigned int generation = namemap_generation.load(std::memory_order_acquire);
  if (namemap_local_cache.generation != generation) {
    (void)memset(namemap_local_cache.entries, 0,
                 sizeof(namemap_local_cache.entries));
    namemap_local_cache.generation = generation;
  }

  const struct NamemapLocalCacheEntry & cached =
    namemap_local_cache.entries[mixedhash % NAME_LOCAL_CACHE_SIZE];
  if (cached.str != NULL && cached.hashvalue == hashvalue &&
      (cached.str == str || strcmp(cached.str, str) == 0)) {
    return cached.str;
  }
  return NULL;
}

static void
namemap_cache_store(unsigned long hashvalue, unsigned int mixedhash,
                    const char * str)
{
  struct NamemapLocalCacheEntry & cached =
    namemap_local_cache.entries[mixedhash % NAME_LOCAL_CACHE_SIZE];
  cached.hashvalue = hashvalue;
  cached.str = str;
}

static void
namemap_grow_shard(struct NamemapShard & shard)
{
  const unsigned int newcount = shard.bucketcount * 2;
  struct NamemapBucketEntry ** newbuckets =
    static_cast<struct NamemapBucketEntry **>(
      malloc(sizeof(struct NamemapBucketEntry *) * newcount));
  (void)memset(newbuckets, 0,
               sizeof(struct NamemapBucketEntry *) * newcount);

  for (unsigned int bucket = 0; bucket < shard.bucketcount; ++bucket) {
    struct NamemapBucketEntry * entry = shard.buckets[bucket];
    while (entry != NULL) {
      struct NamemapBucketEntry * next = entry->next;
      const unsigned int mixedhash = namemap_mix_hash(
        static_cast<unsigned int>(entry->hashvalue));
      const unsigned int newbucket = namemap_bucket_index(mixedhash, newcount);
      entry->next = newbuckets[newbucket];
      newbuckets[newbucket] = entry;
      entry = next;
    }
  }
  free(shard.buckets);
  shard.buckets = newbuckets;
  shard.bucketcount = newcount;
}

static const char *
namemap_find_or_add_string(const char * str, SbBool addifnotfound)
{
  unsigned long h;
  struct NamemapBucketEntry * entry;

  h = cc_string_hash_text(str);
  namemap_ensure_initialized();

  const unsigned int mixedhash = namemap_mix_hash(static_cast<unsigned int>(h));
  const char * cached = namemap_cache_find(str, h, mixedhash);
  if (cached != NULL) return cached;

  struct NamemapShard & shard = nameshards[mixedhash % NAME_SHARD_COUNT];
  CC_MUTEX_LOCK(shard.mutex);
  const unsigned int bucket = namemap_bucket_index(mixedhash, shard.bucketcount);
  entry = shard.buckets[bucket];

  while (entry != NULL) {
    if (entry->hashvalue == h && strcmp(entry->str, str) == 0) { break; }
    entry = entry->next;
  }

  if ((entry == NULL) && addifnotfound) {
    entry = static_cast<struct NamemapBucketEntry *>(malloc(sizeof(struct NamemapBucketEntry)));
    entry->str = find_string_address(shard, str);
    entry->hashvalue = h;
    entry->next = shard.buckets[bucket];

    shard.buckets[bucket] = entry;
    ++shard.entries;
    if (shard.entries > shard.bucketcount * NAME_MAX_AVERAGE_CHAIN) {
      namemap_grow_shard(shard);
    }
  }

  CC_MUTEX_UNLOCK(shard.mutex);
  if (entry != NULL) namemap_cache_store(h, mixedhash, entry->str);
  return entry ? entry->str : NULL;
}

/* ************************************************************************* */

/*!
  Adds a string to the name hash and returns its permanent memory
  address pointer. If the string is already present in the name hash,
  just returns the address pointer.
*/
const char *
cc_namemap_get_address(const char * str)
{
  return namemap_find_or_add_string(str, TRUE);
}

/*!
  Returns address pointer of a given string.

  String will not be added if it doesn't exist in name hash (and \c
  NULL will be returned).
*/
const char *
cc_namemap_peek_string(const char * str)
{
  return namemap_find_or_add_string(str, FALSE);
}
