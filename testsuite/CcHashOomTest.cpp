#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/hashp.h"

static bool fail_calloc = false;
static bool fail_malloc = false;
static bool fail_entry = false;
static bool fail_allocator = false;

static void * injected_calloc(size_t count, size_t size)
{
  if (fail_calloc) { fail_calloc = false; return NULL; }
  return std::calloc(count, size);
}

static void * injected_malloc(size_t size)
{
  if (fail_malloc) { fail_malloc = false; return NULL; }
  return std::malloc(size);
}

static void * injected_allocate(cc_memalloc * allocator)
{
  if (fail_entry) { fail_entry = false; return NULL; }
  return cc_memalloc_allocate(allocator);
}

static cc_memalloc * injected_allocator_construct(unsigned int size,
                                                  unsigned int align)
{
  if (fail_allocator) { fail_allocator = false; return NULL; }
  return cc_memalloc_construct_aligned(size, align);
}

#define calloc(count, size) injected_calloc(count, size)
#define malloc(size) injected_malloc(size)
#define cc_memalloc_allocate(allocator) injected_allocate(allocator)
#define cc_memalloc_construct_aligned(size, align) injected_allocator_construct(size, align)
#include "../src/base/hash.cpp"
#undef calloc
#undef malloc
#undef cc_memalloc_allocate
#undef cc_memalloc_construct_aligned

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
  return 1; \
} } while (0)

static cc_hash_key collision_hash(cc_hash_key)
{
  return 1;
}

static unsigned int hash_calls_before_throw = 0;

static cc_hash_key throwing_hash(cc_hash_key key)
{
  if (hash_calls_before_throw-- == 0)
    throw std::runtime_error("hash failed");
  return key;
}

static cc_hash_entry * find_entry(cc_hash * hash, cc_hash_key key)
{
  const unsigned int index = hash_get_index(hash, key);
  for (cc_hash_entry * entry = hash->buckets[index]; entry != NULL;
       entry = entry->next) {
    if (entry->key == key) return entry;
  }
  return NULL;
}

int main()
{
  struct rlimit no_core = { 0, 0 };
  setrlimit(RLIMIT_CORE, &no_core);
  for (int stage = 0; stage < 4; ++stage) {
    const pid_t child = fork();
    if (child == 0) {
      if (stage == 0) fail_malloc = true;
      if (stage == 1) fail_calloc = true;
      if (stage == 2) fail_allocator = true;
      cc_hash * hash = cc_hash_construct(2, 0.25f);
      if (stage == 3) fail_entry = true;
      (void) cc_hash_put(hash, 7, hash);
      _exit(1);
    }
    if (child < 0) return 2;
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  }

  cc_hash * hash = cc_hash_construct(2, 0.25f);
  CHECK(hash != NULL);
  const unsigned int oldsize = hash->size;
  fail_calloc = true;
  CHECK(cc_hash_put(hash, 7, hash));
  CHECK(!fail_calloc);
  CHECK(hash->size == oldsize);
  void * found = NULL;
  CHECK(cc_hash_get(hash, 7, &found) && found == hash);
  CHECK(cc_hash_put(hash, 8, hash));
  CHECK(hash->size > oldsize);
  CHECK(cc_hash_get(hash, 7, &found) && found == hash);
  cc_hash_destruct(hash);

  hash = cc_hash_construct(17, 0.75f);
  CHECK(hash != NULL);
  int values[4] = { 10, 20, 30, 40 };
  cc_hash_entry * entries[4];
  for (unsigned int i = 0; i < 4; ++i) {
    CHECK(cc_hash_put(hash, i, &values[i]));
    entries[i] = find_entry(hash, i);
    CHECK(entries[i] != NULL);
  }
  cc_hash_entry ** previousbuckets = hash->buckets;
  cc_hash_func * previoushashfunc = hash->hashfunc;
  fail_calloc = true;
  cc_hash_set_hash_func(hash, collision_hash);
  CHECK(!fail_calloc);
  CHECK(hash->buckets == previousbuckets);
  CHECK(hash->hashfunc == previoushashfunc);
  for (unsigned int i = 0; i < 4; ++i) {
    CHECK(find_entry(hash, i) == entries[i]);
    CHECK(cc_hash_get(hash, i, &found) && found == &values[i]);
  }
  cc_hash_set_hash_func(hash, collision_hash);
  CHECK(hash->hashfunc == collision_hash);
  for (unsigned int i = 0; i < 4; ++i) {
    CHECK(find_entry(hash, i) == entries[i]);
    CHECK(cc_hash_get(hash, i, &found) && found == &values[i]);
  }
  cc_hash_destruct(hash);

  hash = cc_hash_construct(17, 1.0f);
  CHECK(cc_hash_put(hash, 0, NULL));
  CHECK(cc_hash_put(hash, 17, NULL));
  hash_calls_before_throw = 1;
  try {
    cc_hash_set_hash_func(hash, throwing_hash);
    CHECK(false);
  }
  catch (const std::runtime_error &) { }
  CHECK(hash->hashfunc != throwing_hash);
  CHECK(cc_hash_get(hash, 0, &found));
  CHECK(cc_hash_get(hash, 17, &found));
  cc_hash_destruct(hash);

  hash = cc_hash_construct(2, 1.0f);
  cc_hash_set_hash_func(hash, throwing_hash);
  hash_calls_before_throw = 10;
  CHECK(cc_hash_put(hash, 0, NULL));
  CHECK(cc_hash_put(hash, 2, NULL));
  hash_calls_before_throw = 2; // One lookup and one rehash, then throw.
  try {
    cc_hash_put(hash, 4, NULL);
    CHECK(false);
  }
  catch (const std::runtime_error &) { }
  CHECK(cc_hash_get_num_elements(hash) == 3);
  hash_calls_before_throw = 10;
  CHECK(cc_hash_get(hash, 0, &found));
  CHECK(cc_hash_get(hash, 2, &found));
  CHECK(cc_hash_get(hash, 4, &found));
  cc_hash_destruct(hash);

  hash = cc_hash_construct(17, 1.0f);
  CHECK(cc_hash_put(hash, 0, NULL));
  CHECK(cc_hash_put(hash, 17, NULL));
  fail_malloc = true;
  cc_hash_set_hash_func(hash, collision_hash);
  CHECK(!fail_malloc);
  CHECK(hash->hashfunc != collision_hash);
  CHECK(cc_hash_get(hash, 0, &found));
  CHECK(cc_hash_get(hash, 17, &found));
  cc_hash_destruct(hash);

  hash = cc_hash_construct(2, 1.0f);
  CHECK(cc_hash_put(hash, 0, NULL));
  CHECK(cc_hash_put(hash, 2, NULL));
  fail_malloc = true;
  CHECK(cc_hash_put(hash, 4, NULL));
  CHECK(fail_malloc); // Default hashing needs no temporary rehash plan.
  fail_malloc = false;
  CHECK(hash->size > 2);
  CHECK(cc_hash_get(hash, 0, &found));
  CHECK(cc_hash_get(hash, 2, &found));
  CHECK(cc_hash_get(hash, 4, &found));
  cc_hash_destruct(hash);

  return 0;
}
