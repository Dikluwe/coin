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

/*
  This ADT manages thread-local memory.  When different threads access
  the memory an cc_storage object manages, they will receive different
  memory blocks back.

  For additional API documentation, see doc of the SbStorage C++
  wrapper around the cc_storage_*() functions at the bottom of this
  file.
*/

/* ********************************************************************** */

/*!
  \struct cc_storage common.h Inventor/C/threads/common.h
  \ingroup coin_threads
  \brief The structure for the thread local memory storage.
*/

/*!
  \typedef struct cc_storage cc_storage
  \ingroup coin_threads
  \brief The type definition for the thread local memory storage structure.
*/

#include <Inventor/C/threads/storage.h>
#include "coindefs.h"

#include <cstdlib>
#include <cassert>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

/* FIXME: instead of doing all the HAVE_THREADS wrapping in the code
   of this file, perhaps it would be cleaner to make the cc_thread and
   cc_mutex interfaces *working*, dummy skeletons when no thread
   abstractions are available?  20040615 mortene. */
#ifdef HAVE_THREADS
#include <Inventor/C/base/list.h>
#include <Inventor/C/threads/thread.h>
#include <Inventor/C/threads/mutex.h>
#include "threads/mutexp.h"
#include "threads/threadp.h"
#include "tidbitsp.h"
#endif /* HAVE_THREADS */

#include "threads/storagep.h"

/* ********************************************************************** */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* private functions */

typedef struct {
  void * value;
  void (*destructor)(void *);
} cc_storage_cleanup_item;

typedef struct {
  void ** values;
  unsigned int length;
} cc_storage_hash_snapshot;

static void storage_hash_collect(uintptr_t key, void * val, void * closure);


#ifdef HAVE_THREADS

static cc_list * cc_storage_live_storages = NULL;

static void
cc_storage_registry_cleanup(void)
{
  cc_mutex_global_lock();
  if (cc_storage_live_storages != NULL) {
    assert(cc_list_get_length(cc_storage_live_storages) == 0);
    cc_list_destruct(cc_storage_live_storages);
    cc_storage_live_storages = NULL;
  }
  cc_mutex_global_unlock();
}

static void
cc_storage_registry_ensure_locked(void)
{
  if (cc_storage_live_storages == NULL) {
    cc_storage_live_storages = cc_list_construct();
    coin_atexit(cc_storage_registry_cleanup, CC_ATEXIT_THREADING_SUBSYSTEM);
  }
}

static void
cc_storage_registry_add(cc_storage * storage)
{
  cc_mutex_global_lock();
  cc_storage_registry_ensure_locked();
  cc_list_append(cc_storage_live_storages, storage);
  cc_mutex_global_unlock();
}

#endif /* HAVE_THREADS */

static cc_storage *
cc_storage_init(unsigned int size, void (*constructor)(void *), 
                void (*destructor)(void *)) 
{
  cc_storage * storage = (cc_storage *) malloc(sizeof(cc_storage));
  storage->size = size;
  storage->constructor = constructor;
  storage->destructor = destructor;
  storage->dict = cc_dict_construct(8, 0.75f);
#ifdef HAVE_THREADS
  storage->mutex = cc_mutex_construct();
  cc_storage_registry_add(storage);
#endif /* HAVE_THREADS */

  return storage;
}

static void
cc_storage_hash_destruct_cb(uintptr_t COIN_UNUSED_ARG(key), void * val, void * closure)
{
  cc_storage * storage = (cc_storage*) closure;
  
  if (storage->destructor) {
    storage->destructor(val);
  }
  free(val);
}

/* ********************************************************************** */
/* public api */

cc_storage *
cc_storage_construct(unsigned int size)
{
  return cc_storage_init(size, NULL, NULL);
}

cc_storage *
cc_storage_construct_etc(unsigned int size,
                         void (*constructor)(void *),
                         void (*destructor)(void *))
{
  return cc_storage_init(size, constructor, destructor);
}

/*
*/
void
cc_storage_destruct(cc_storage * storage)
{
  assert(storage != NULL);

#ifdef HAVE_THREADS
  cc_storage_hash_snapshot snapshot;
  snapshot.values = NULL;
  snapshot.length = 0;

  cc_mutex_global_lock();
  cc_mutex_lock(storage->mutex);

  const unsigned int numelements = cc_dict_get_num_elements(storage->dict);
  if (numelements > 0) {
    snapshot.values = (void **) malloc(sizeof(void *) * numelements);
    assert(snapshot.values != NULL);
    if (snapshot.values == NULL) {
      cc_mutex_unlock(storage->mutex);
      cc_mutex_global_unlock();
      return;
    }
    cc_dict_apply(storage->dict, storage_hash_collect, &snapshot);
    assert(snapshot.length == numelements);
    cc_dict_clear(storage->dict);
  }

  if (cc_storage_live_storages != NULL) {
    cc_list_remove_item(cc_storage_live_storages, storage);
  }
  cc_mutex_unlock(storage->mutex);
  cc_mutex_global_unlock();

  for (unsigned int i = 0; i < snapshot.length; i++) {
    if (storage->destructor != NULL) {
      storage->destructor(snapshot.values[i]);
    }
    free(snapshot.values[i]);
  }
  free(snapshot.values);
#else /* ! HAVE_THREADS */
  cc_dict_apply(storage->dict, cc_storage_hash_destruct_cb, storage);
#endif /* ! HAVE_THREADS */

  cc_dict_destruct(storage->dict);

#ifdef HAVE_THREADS
  cc_mutex_destruct(storage->mutex);
#endif /* HAVE_THREADS */

  free(storage);
}

/* ********************************************************************** */

/*
*/

void *
cc_storage_get(cc_storage * storage)
{
  void * val;
  unsigned long threadid = 0;

#ifdef HAVE_THREADS
  cc_thread_storage_guard_ensure();
  threadid = cc_thread_id();

  cc_mutex_lock(storage->mutex);
#endif /* HAVE_THREADS */

  if (!cc_dict_get(storage->dict, threadid, &val)) {
    val = malloc(storage->size);
    if (storage->constructor) {
      storage->constructor(val);
    }
    (void) cc_dict_put(storage->dict, threadid, val);
  }

#ifdef HAVE_THREADS
  cc_mutex_unlock(storage->mutex);
#endif /* HAVE_THREADS */

  return val;
}

static void 
storage_hash_collect(uintptr_t COIN_UNUSED_ARG(key), void * val, void * closure)
{
  cc_storage_hash_snapshot * snapshot =
    (cc_storage_hash_snapshot *) closure;
  snapshot->values[snapshot->length++] = val;
}

void 
cc_storage_apply_to_all(cc_storage * storage, 
                        cc_storage_apply_func * func, 
                        void * closure)
{
  if (func == NULL) return;

  cc_storage_hash_snapshot snapshot;
  snapshot.values = NULL;
  snapshot.length = 0;

#ifdef HAVE_THREADS
  cc_mutex_lock(storage->mutex);
#endif /* HAVE_THREADS */

  const unsigned int numelements = cc_dict_get_num_elements(storage->dict);
  if (numelements > 0) {
    snapshot.values = (void **) malloc(sizeof(void *) * numelements);
    assert(snapshot.values != NULL);
    if (snapshot.values == NULL) {
#ifdef HAVE_THREADS
      cc_mutex_unlock(storage->mutex);
#endif /* HAVE_THREADS */
      return;
    }
    cc_dict_apply(storage->dict, storage_hash_collect, &snapshot);
    assert(snapshot.length == numelements);
  }

#ifdef HAVE_THREADS
  cc_mutex_unlock(storage->mutex);
#endif /* HAVE_THREADS */

  for (unsigned int i = 0; i < snapshot.length; i++) {
    func(snapshot.values[i], closure);
  }
  free(snapshot.values);
}


/* ********************************************************************** */

unsigned int
cc_storage_thread_cleanup(unsigned long threadid)
{
#ifdef HAVE_THREADS
  cc_storage_cleanup_item * items = NULL;
  unsigned int numitems = 0;

  cc_mutex_global_lock();
  if (cc_storage_live_storages == NULL) {
    cc_mutex_global_unlock();
    return 0;
  }

  const int numstorages = cc_list_get_length(cc_storage_live_storages);
  if (numstorages > 0) {
    items = (cc_storage_cleanup_item *)
      malloc(sizeof(cc_storage_cleanup_item) * (unsigned int) numstorages);
    assert(items != NULL);
    if (items == NULL) {
      cc_mutex_global_unlock();
      return 0;
    }
  }

  for (int i = 0; i < numstorages; i++) {
    cc_storage * storage =
      (cc_storage *) cc_list_get(cc_storage_live_storages, i);
    void * value = NULL;

    cc_mutex_lock(storage->mutex);
    if (cc_dict_get(storage->dict, threadid, &value)) {
      (void) cc_dict_remove(storage->dict, threadid);
      items[numitems].value = value;
      items[numitems].destructor = storage->destructor;
      numitems++;
    }
    cc_mutex_unlock(storage->mutex);
  }
  cc_mutex_global_unlock();

  for (unsigned int i = 0; i < numitems; i++) {
    if (items[i].destructor != NULL) {
      items[i].destructor(items[i].value);
    }
    free(items[i].value);
  }
  free(items);
  return numitems;
#else /* ! HAVE_THREADS */
  (void) threadid;
  return 0;
#endif /* ! HAVE_THREADS */
}

/* ********************************************************************** */

// All the documentation below is obsolete - however it may be useful for re-writing
// So, for now, simply revert to a normal c++ comment.  walroy 20140613
/*
  \class SbStorage Inventor/threads/SbStorage.h
  \brief The SbStorage class manages thread-local memory.

  \ingroup coin_threads

  This class manages thread-local memory.  When different threads
  access the memory an SbStorage object manages, they will receive
  different memory blocks back.

  This provides a mechanism for sharing read/write static data.

  One important implementation detail: if the Coin library was
  explicitly configured to be built without multi-platform thread
  abstractions, or neither pthreads nor native Win32 thread functions
  are available, it will be assumed that the client code will all run
  in the same thread. This means that the same memory block will be
  returned for any request without considering the current thread id.
*/

/*
  \fn SbStorage::SbStorage(unsigned int size)

  Constructor.  \a size specifies the number of bytes each thread should
  have in this thread-local memory management object.
*/

/*
  \fn SbStorage::SbStorage(unsigned int size, void (*constr)(void *), void (*destr)(void *))

  Constructor.  \a size specifies the number of bytes each thread should
  have in this thread-local memory management object.  A constructor and
  a destructor functions can be given that will be called when the actual
  memory blocks are allocated and freed.
*/

/*
  \fn SbStorage::~SbStorage(void)

  The destructor.
*/

/*
  \fn void * SbStorage::get(void)

  This method returns the calling thread's thread-local memory block.
*/

/*
  \fn void SbStorage::applyToAll(SbStorageApplyFunc * func, void * closure)
  
  This method will call \a func for all thread local storage data.
  \a closure will be supplied as the second parameter to the callback.
*/

/* ********************************************************************** */

/*
  \class SbTypedStorage Inventor/threads/SbTypedStorage.h
  \brief The SbTypedStorage class manages generic thread-local memory.

  \ingroup coin_threads

  This class manages thread-local memory.  When different threads
  access the memory an SbTypedStorage object manages, they will receive
  different memory blocks back.

  This provides a mechanism for sharing read/write static data.
*/

/*
  \fn SbTypedStorage<Type>::SbTypedStorage(unsigned int size)

  Constructor.  \a size specifies the number of bytes each thread should
  have in this thread-local memory management object.
*/

/*
  \fn SbTypedStorage<Type>::SbTypedStorage(unsigned int size, void (*constr)(void *), void (*destr)(void *))

  Constructor.  \a size specifies the number of bytes each thread
  should have in this thread-local memory management object.
  Constructor and a destructor functions can be specified that will be
  called when the actual memory blocks are allocated and freed.
*/

/*
  \fn SbTypedStorage<Type>::~SbTypedStorage(void)

  The destructor.
*/

/*
  \fn Type SbTypedStorage<Type>::get(void)

  This method returns the calling thread's thread-local memory block.
*/

/* ********************************************************************** */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#ifdef COIN_TEST_SUITE

#include <Inventor/C/threads/thread.h>
#include "threads/storage_test_config.h"
#include <Inventor/threads/SbStorage.h>
#include <Inventor/threads/SbTypedStorage.h>

#include <type_traits>

static int storage_test_construct_count;
static int storage_test_destruct_count;

static void
storage_test_construct(void * data)
{
  storage_test_construct_count++;
  *static_cast<int *>(data) = 17;
}

static void
storage_test_destruct(void * data)
{
  storage_test_destruct_count++;
  *static_cast<int *>(data) = 0;
}

static void
storage_test_apply(void * data, void * closure)
{
  BOOST_CHECK_EQUAL(*static_cast<int *>(data), 17);
  (*static_cast<int *>(closure))++;
}

typedef struct {
  cc_storage * storage;
  void * expected;
  int calls;
} storage_test_reentrant_data;

static void
storage_test_reentrant_apply(void * data, void * closure)
{
  storage_test_reentrant_data * state =
    static_cast<storage_test_reentrant_data *>(closure);
  BOOST_CHECK(data == state->expected);
  BOOST_CHECK(cc_storage_get(state->storage) == state->expected);
  state->calls++;
}

BOOST_AUTO_TEST_CASE(cc_storage_preserves_same_thread_value_and_lifecycle)
{
  storage_test_construct_count = 0;
  storage_test_destruct_count = 0;

  cc_storage * storage = cc_storage_construct_etc(sizeof(int),
                                                   storage_test_construct,
                                                   storage_test_destruct);
  int * first = static_cast<int *>(cc_storage_get(storage));
  int * second = static_cast<int *>(cc_storage_get(storage));

  BOOST_REQUIRE(first != NULL);
  BOOST_CHECK(first == second);
  BOOST_CHECK_EQUAL(*first, 17);
  BOOST_CHECK_EQUAL(storage_test_construct_count, 1);

  int apply_count = 0;
  cc_storage_apply_to_all(storage, storage_test_apply, &apply_count);
  BOOST_CHECK_EQUAL(apply_count, 1);

  storage_test_reentrant_data reentrant = { storage, first, 0 };
  cc_storage_apply_to_all(storage, storage_test_reentrant_apply, &reentrant);
  BOOST_CHECK_EQUAL(reentrant.calls, 1);

  cc_storage_apply_to_all(storage, NULL, &apply_count);
  BOOST_CHECK_EQUAL(apply_count, 1);

  cc_storage_destruct(storage);
  BOOST_CHECK_EQUAL(storage_test_destruct_count, 1);
}

#ifdef HAVE_THREADS

extern "C" unsigned int cc_storage_thread_cleanup(unsigned long threadid);

typedef struct {
  cc_storage * storage;
  unsigned long threadid;
  void * value;
} storage_test_thread_data;

static void *
storage_test_get_from_worker(void * closure)
{
  storage_test_thread_data * data =
    static_cast<storage_test_thread_data *>(closure);
  data->threadid = cc_thread_id();
  data->value = cc_storage_get(data->storage);
  return NULL;
}

BOOST_AUTO_TEST_CASE(cc_storage_thread_exit_cleans_worker_block)
{
  storage_test_construct_count = 0;
  storage_test_destruct_count = 0;

  cc_storage * storage = cc_storage_construct_etc(sizeof(int),
                                                   storage_test_construct,
                                                   storage_test_destruct);
  const unsigned int numthreads = 32;
  for (unsigned int i = 0; i < numthreads; i++) {
    storage_test_thread_data data = { storage, 0, NULL };
    cc_thread * thread =
      cc_thread_construct(storage_test_get_from_worker, &data);
    BOOST_REQUIRE(thread != NULL);
    BOOST_REQUIRE_EQUAL(cc_thread_join(thread, NULL), CC_OK);
    cc_thread_destruct(thread);
    BOOST_REQUIRE(data.value != NULL);
    BOOST_CHECK_EQUAL(cc_storage_thread_cleanup(data.threadid), 0U);
  }

  BOOST_CHECK_EQUAL(storage_test_construct_count, numthreads);
  BOOST_CHECK_EQUAL(storage_test_destruct_count, numthreads);

  int apply_count = 0;
  cc_storage_apply_to_all(storage, storage_test_apply, &apply_count);
  BOOST_CHECK_EQUAL(apply_count, 0);

  cc_storage_destruct(storage);
  BOOST_CHECK_EQUAL(storage_test_destruct_count, numthreads);
}

static cc_storage * storage_test_exit_reentrant_storage;
static int storage_test_exit_reentrant_construct_count;
static int storage_test_exit_reentrant_destruct_count;

static void
storage_test_exit_reentrant_construct(void * data)
{
  storage_test_exit_reentrant_construct_count++;
  *static_cast<int *>(data) = 23;
}

static void
storage_test_exit_reentrant_destruct(void *)
{
  storage_test_exit_reentrant_destruct_count++;
  if (storage_test_exit_reentrant_destruct_count == 1) {
    int * value = static_cast<int *>(
      cc_storage_get(storage_test_exit_reentrant_storage));
    BOOST_REQUIRE(value != NULL);
    BOOST_CHECK_EQUAL(*value, 23);
  }
}

BOOST_AUTO_TEST_CASE(cc_storage_thread_exit_handles_reentrant_destructor)
{
  storage_test_exit_reentrant_construct_count = 0;
  storage_test_exit_reentrant_destruct_count = 0;
  storage_test_exit_reentrant_storage =
    cc_storage_construct_etc(sizeof(int),
                             storage_test_exit_reentrant_construct,
                             storage_test_exit_reentrant_destruct);

  storage_test_thread_data data = {
    storage_test_exit_reentrant_storage, 0, NULL
  };
  cc_thread * thread = cc_thread_construct(storage_test_get_from_worker, &data);
  BOOST_REQUIRE(thread != NULL);
  BOOST_REQUIRE_EQUAL(cc_thread_join(thread, NULL), CC_OK);
  cc_thread_destruct(thread);

  BOOST_CHECK_EQUAL(storage_test_exit_reentrant_construct_count, 2);
  BOOST_CHECK_EQUAL(storage_test_exit_reentrant_destruct_count, 2);

  cc_storage_destruct(storage_test_exit_reentrant_storage);
  storage_test_exit_reentrant_storage = NULL;
}

#endif /* HAVE_THREADS */

static_assert(!std::is_copy_constructible<SbStorage>::value,
              "SbStorage must not copy its owning cc_storage pointer");
static_assert(!std::is_copy_assignable<SbStorage>::value,
              "SbStorage must not copy-assign its owning cc_storage pointer");
static_assert(!std::is_copy_constructible<SbTypedStorage<int *> >::value,
              "SbTypedStorage must not copy its owning cc_storage pointer");
static_assert(!std::is_copy_assignable<SbTypedStorage<int *> >::value,
              "SbTypedStorage must not copy-assign its owning cc_storage pointer");

#endif // COIN_TEST_SUITE
