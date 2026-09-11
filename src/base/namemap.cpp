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
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "threads/threadsutilp.h"
#include "tidbitsp.h"
#include "coindefs.h"

#ifndef COIN_WORKAROUND_NO_USING_STD_FUNCS
using std::malloc;
using std::realloc;
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
static const unsigned int NAME_ENTRY_BLOCK_SIZE = 256;
static const uint32_t NAME_INVALID_ENTRY = UINT32_MAX;
// This covers the common vocabulary of a traversal without making every
// application thread carry a large secondary name table.
static const unsigned int NAME_LOCAL_CACHE_SIZE = 256;

struct NamemapMemChunk {
  char * curbyte;
  size_t bytesleft;
  struct NamemapMemChunk * next;
};

struct NamemapBucketEntry {
  const char * str;
  uint32_t hashvalue;
  uint32_t next;
};

struct NamemapEntryBlock {
  struct NamemapBucketEntry entries[NAME_ENTRY_BLOCK_SIZE];
};

struct NamemapShard {
  void * mutex;
  uint32_t * buckets;
  unsigned int bucketcount;
  uint32_t entries;
  struct NamemapEntryBlock ** entryblocks;
  unsigned int entryblockcount;
  unsigned int entryblockcapacity;
  struct NamemapMemChunk * headchunk;
};

struct NamemapLocalCache {
  unsigned int generation;
  const char * strings[NAME_LOCAL_CACHE_SIZE];
  uint16_t fingerprints[NAME_LOCAL_CACHE_SIZE];
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

    for (unsigned int block = 0; block < shard.entryblockcount; ++block) {
      free(shard.entryblocks[block]);
    }
    free(shard.entryblocks);
    shard.entryblocks = NULL;
    shard.entryblockcount = 0;
    shard.entryblockcapacity = 0;
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
    shard.buckets = static_cast<uint32_t *>(
      malloc(sizeof(uint32_t) * NAME_INITIAL_BUCKET_COUNT));
    for (unsigned int bucket = 0; bucket < NAME_INITIAL_BUCKET_COUNT; ++bucket) {
      shard.buckets[bucket] = NAME_INVALID_ENTRY;
    }
    shard.bucketcount = NAME_INITIAL_BUCKET_COUNT;
    shard.entries = 0;
    shard.entryblocks = NULL;
    shard.entryblockcount = 0;
    shard.entryblockcapacity = 0;
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

static struct NamemapBucketEntry &
namemap_entry(struct NamemapShard & shard, uint32_t entryidx)
{
  assert(entryidx < shard.entries);
  return shard.entryblocks[entryidx / NAME_ENTRY_BLOCK_SIZE]->
    entries[entryidx % NAME_ENTRY_BLOCK_SIZE];
}

static uint32_t
namemap_allocate_entry(struct NamemapShard & shard)
{
  assert(shard.entries != NAME_INVALID_ENTRY);
  const uint32_t entryidx = shard.entries;
  if ((entryidx % NAME_ENTRY_BLOCK_SIZE) == 0) {
    if (shard.entryblockcount == shard.entryblockcapacity) {
      const unsigned int newcapacity = shard.entryblockcapacity == 0
        ? 4 : shard.entryblockcapacity * 2;
      shard.entryblocks = static_cast<struct NamemapEntryBlock **>(
        realloc(shard.entryblocks,
                sizeof(struct NamemapEntryBlock *) * newcapacity));
      shard.entryblockcapacity = newcapacity;
    }
    shard.entryblocks[shard.entryblockcount++] =
      static_cast<struct NamemapEntryBlock *>(
        malloc(sizeof(struct NamemapEntryBlock)));
  }
  ++shard.entries;
  return entryidx;
}

static const char *
namemap_cache_find(const char * str, unsigned int mixedhash)
{
  const unsigned int generation = namemap_generation.load(std::memory_order_acquire);
  if (namemap_local_cache.generation != generation) {
    (void)memset(namemap_local_cache.strings, 0,
                 sizeof(namemap_local_cache.strings));
    namemap_local_cache.generation = generation;
  }

  const unsigned int cacheidx = mixedhash % NAME_LOCAL_CACHE_SIZE;
  const char * cached = namemap_local_cache.strings[cacheidx];
  if (cached != NULL &&
      namemap_local_cache.fingerprints[cacheidx] ==
        static_cast<uint16_t>(mixedhash >> 16) &&
      (cached == str || strcmp(cached, str) == 0)) {
    return cached;
  }
  return NULL;
}

static void
namemap_cache_store(unsigned int mixedhash, const char * str)
{
  const unsigned int cacheidx = mixedhash % NAME_LOCAL_CACHE_SIZE;
  namemap_local_cache.strings[cacheidx] = str;
  namemap_local_cache.fingerprints[cacheidx] =
    static_cast<uint16_t>(mixedhash >> 16);
}

static void
namemap_grow_shard(struct NamemapShard & shard)
{
  const unsigned int newcount = shard.bucketcount * 2;
  uint32_t * newbuckets = static_cast<uint32_t *>(
    malloc(sizeof(uint32_t) * newcount));
  for (unsigned int bucket = 0; bucket < newcount; ++bucket) {
    newbuckets[bucket] = NAME_INVALID_ENTRY;
  }

  for (uint32_t entryidx = 0; entryidx < shard.entries; ++entryidx) {
    struct NamemapBucketEntry & entry = namemap_entry(shard, entryidx);
    const unsigned int mixedhash = namemap_mix_hash(entry.hashvalue);
    const unsigned int newbucket = namemap_bucket_index(mixedhash, newcount);
    entry.next = newbuckets[newbucket];
    newbuckets[newbucket] = entryidx;
  }
  free(shard.buckets);
  shard.buckets = newbuckets;
  shard.bucketcount = newcount;
}

static const char *
namemap_find_or_add_string(const char * str, SbBool addifnotfound)
{
  const uint32_t h = cc_string_hash_text(str);

  namemap_ensure_initialized();

  const unsigned int mixedhash = namemap_mix_hash(h);
  const char * cached = namemap_cache_find(str, mixedhash);
  if (cached != NULL) return cached;

  struct NamemapShard & shard = nameshards[mixedhash % NAME_SHARD_COUNT];
  CC_MUTEX_LOCK(shard.mutex);
  const unsigned int bucket = namemap_bucket_index(mixedhash, shard.bucketcount);
  uint32_t entryidx = shard.buckets[bucket];

  while (entryidx != NAME_INVALID_ENTRY) {
    struct NamemapBucketEntry & entry = namemap_entry(shard, entryidx);
    if (entry.hashvalue == h && strcmp(entry.str, str) == 0) { break; }
    entryidx = entry.next;
  }

  if ((entryidx == NAME_INVALID_ENTRY) && addifnotfound) {
    entryidx = namemap_allocate_entry(shard);
    struct NamemapBucketEntry & entry = namemap_entry(shard, entryidx);
    entry.str = find_string_address(shard, str);
    entry.hashvalue = h;
    entry.next = shard.buckets[bucket];

    shard.buckets[bucket] = entryidx;
    if (shard.entries > shard.bucketcount * NAME_MAX_AVERAGE_CHAIN) {
      namemap_grow_shard(shard);
    }
  }

  const char * result = entryidx != NAME_INVALID_ENTRY
    ? namemap_entry(shard, entryidx).str : NULL;
  CC_MUTEX_UNLOCK(shard.mutex);
  if (result != NULL) namemap_cache_store(mixedhash, result);
  return result;
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
