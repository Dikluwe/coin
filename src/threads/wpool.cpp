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

/*!
  \struct cc_wpool common.h Inventor/C/threads/common.h
  \ingroup coin_threads
  \brief The structure for a worker pool.
*/

/*!
  \typedef struct cc_wpool cc_wpool
  \ingroup coin_threads
  \brief The type definition for a worker pool structure.
*/

#include <Inventor/C/threads/wpool.h>

#include <cstdlib>
#include <cassert>

#include <Inventor/C/threads/worker.h>
#include <Inventor/C/threads/mutex.h>
#include <Inventor/C/threads/condvar.h>
#include <Inventor/C/errors/debugerror.h>

#include "threads/wpoolp.h"

/* ********************************************************************** */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* private methods */

static void
wpool_lock(cc_wpool * pool)
{
  cc_mutex_lock(pool->mutex);
}

static void
wpool_unlock(cc_wpool * pool)
{
  cc_mutex_unlock(pool->mutex);
}

/*!
  Called by the worker once the job is finished, just before it goes into a 
  condvar_wait().
*/
static void
wpool_idle_cb(cc_worker * worker, void * data)
{
  int idx;
  cc_wpool * pool = (cc_wpool*) data;

  wpool_lock(pool);

  idx = cc_list_find(pool->busypool, worker);
  assert(idx >= 0);
  if (idx >= 0) {
    cc_list_remove_fast(pool->busypool, idx);
    cc_list_append(pool->idlepool, worker);
  }

  if (pool->numwaiters > 0) {
    cc_condvar_wake_all(pool->waitcond);
  }
  wpool_unlock(pool);
}

/*!
  Will wait until we have \e num idle workers.
*/
static void
wpool_wait(cc_wpool * pool, int num)
{
  pool->numwaiters++;
  while (cc_list_get_length(pool->idlepool) < num) {
    /* wait() will atomically unlock the mutex, and wait
     * for signal. When signal arrived, the mutex will again be
     * atomically locked. */
    cc_condvar_wait(pool->waitcond, pool->mutex);
  }
  assert(pool->numwaiters > 0);
  pool->numwaiters--;
}

static void
wpool_add_workers(cc_wpool * pool, int num)
{
  int i;
  cc_worker * worker;
  for (i = 0; i < num; i++) {
    worker = cc_worker_construct();
    cc_worker_set_idle_callback(worker, wpool_idle_cb, pool);
    cc_list_append(pool->idlepool, worker);
  }
}

static cc_worker *
wpool_get_idle_worker(cc_wpool * pool)
{
  cc_worker * worker;
  /* assumes pool is locked (begin() has been called) */
  if (cc_list_get_length(pool->idlepool) == 0) return NULL;

  worker = (cc_worker*) cc_list_pop(pool->idlepool);
  if (worker != NULL) {
    cc_list_append(pool->busypool, worker);
  }
  return worker;
}

/* ********************************************************************** */
/* public api */

/*!
  Construct worker pool.
*/
cc_wpool *
cc_wpool_construct(int numworkers)
{
  cc_wpool * pool = (cc_wpool*) malloc(sizeof(cc_wpool));

  pool->mutex = cc_mutex_construct();
  pool->sizemutex = cc_mutex_construct();
  pool->waitcond = cc_condvar_construct();
  pool->idlepool = cc_list_construct();
  pool->busypool = cc_list_construct();
  pool->numwaiters = 0;
  pool->numworkers = 0;

  cc_wpool_set_num_workers(pool, numworkers);
  return pool;
}

/*!
  Destruct worker pool.
  Will wait for all jobs in progress to finish
*/
void
cc_wpool_destruct(cc_wpool * pool)
{
  int i, n;
  cc_worker ** workers;
  cc_wpool_wait_all(pool);
  assert(cc_list_get_length(pool->busypool) == 0);
  n = cc_list_get_length(pool->idlepool);

  workers = (cc_worker**)
    cc_list_get_array(pool->idlepool);

  for (i = 0; i < n; i++) {
    cc_worker_destruct(workers[i]);
  }

  cc_list_destruct(pool->idlepool);
  cc_list_destruct(pool->busypool);
  cc_mutex_destruct(pool->mutex);
  cc_mutex_destruct(pool->sizemutex);
  cc_condvar_destruct(pool->waitcond);
  free(pool);
}

/*!
  Returns the number of workers in the pool.
*/
int
cc_wpool_get_num_workers(cc_wpool * pool)
{
  cc_mutex_lock(pool->sizemutex);
  const int numworkers = pool->numworkers;
  cc_mutex_unlock(pool->sizemutex);
  return numworkers;
}

/*!
  Sets the number of workers in the pool.
*/
void
cc_wpool_set_num_workers(cc_wpool * pool, int newnum)
{
  if (newnum < 0) newnum = 0;
  wpool_lock(pool);
  if (newnum == pool->numworkers) {
    wpool_unlock(pool);
    return;
  }

  wpool_wait(pool, pool->numworkers);

  if (newnum > pool->numworkers) {
    wpool_add_workers(pool, newnum - pool->numworkers);
  }
  else {
    int i, n = pool->numworkers - newnum;
    for (i = 0; i < n; i++) {
      cc_worker_destruct((cc_worker*) cc_list_pop(pool->idlepool));
    }
  }
  cc_mutex_lock(pool->sizemutex);
  pool->numworkers = newnum;
  cc_mutex_unlock(pool->sizemutex);
  wpool_unlock(pool);
}

/*!
  Wait for all pool workers to finish working and go into idle state.
  This method should only be called by the thread controlling the pool.
  A pool worker should not call this method, since it will obviously
  never return from here (it will never go idle).
*/
void
cc_wpool_wait_all(cc_wpool * pool)
{
  wpool_lock(pool);
  wpool_wait(pool, pool->numworkers); /* wait for all workers to become idle */
  wpool_unlock(pool);
}

/*!

  Locks the pool so that workers can be started using the
  cc_wpool_start_worker() method. \a numworkersneeded should contain
  the minimum number of workers that is needed. If \a numworkersneeded
  workers are available, the pool will be locked and TRUE is returned.
  Otherwise FALSE is returned.

  Usage pseudo code:

  \code

  int numworkers = 5;

  if (cc_wpool_begin(pool, numworkers)) {
    for (int i = 0; i < numworkers; i++) {
      cc_wpool_start_worker(my_work[i], my_closure[i]);
    }
    cc_wpool_end(pool);
  }

  \endcode

  Important! If too few workers are available, the pool will not be
  locked and cc_wpool_end() should not be called.

  \sa cc_wpool_start_worker(), cc_wpool_end()
*/
SbBool
cc_wpool_try_begin(cc_wpool * pool, int numworkersneeded)
{
  int n;
  wpool_lock(pool);
  if (numworkersneeded < 0 || numworkersneeded > pool->numworkers) {
    wpool_unlock(pool);
    return FALSE;
  }

  n = cc_list_get_length(pool->idlepool);
  if (n < numworkersneeded) {
    wpool_unlock(pool);
    return FALSE;
  }
  return TRUE;
}

/*!
  Wait for \a numworkersneeded workers to become idle. When returning
  from this call, the pool will be locked, and up to \a numworkersneeded
  can be started using the cc_wpool_start_worker() method. Remember
  to call cc_wpool_end() to unlock the pool again.

  \sa cc_wpool_try_begin()
*/
void
cc_wpool_begin(cc_wpool * pool, int numworkersneeded)
{
  int n;
  wpool_lock(pool);

  if (numworkersneeded < 0) numworkersneeded = 0;
  if (numworkersneeded > pool->numworkers) {
    numworkersneeded = pool->numworkers;
  }

  n = cc_list_get_length(pool->idlepool);
  if (n < numworkersneeded) {
    wpool_wait(pool, numworkersneeded);
  }
}

/*!

  Starts a worker. The pool must be locked (using cc_wpool_begin())
  before calling this method.

  \sa cc_wpool_begin() , cc_wpool_end()
*/
void
cc_wpool_start_worker(cc_wpool * pool, cc_wpool_f * workfunc, void * closure)
{
  if (workfunc == NULL) return;

  cc_worker * worker = wpool_get_idle_worker(pool);
  if (worker != NULL) {
    cc_worker_start(worker, workfunc, closure);
  }
}

/*!

  Unlocks the pool after a cc_wpool_begin(), cc_wpool_start_worker()
  sequence.

  Please note that if cc_wpool_begin() returns 0, you should not call
  cc_wpool_end().

  \sa cc_wpool_begin(), cc_wpool_start_worker()

*/
void
cc_wpool_end(cc_wpool * pool)
{
  wpool_unlock(pool);
}

/* ********************************************************************** */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#ifdef COIN_TEST_SUITE

#include <Inventor/C/threads/thread.h>
#include <threads/wpool_test_config.h>
#include <atomic>
#include <chrono>
#include <thread>

static void
wpool_test_callback(void * closure)
{
  int * calls = static_cast<int *>(closure);
  (*calls)++;
}

BOOST_AUTO_TEST_CASE(cc_wpool_rejects_invalid_worker_counts)
{
  cc_wpool * pool = cc_wpool_construct(-1);

  BOOST_CHECK_EQUAL(cc_wpool_get_num_workers(pool), 0);
  BOOST_CHECK(!cc_wpool_try_begin(pool, -1));
  BOOST_CHECK(!cc_wpool_try_begin(pool, 1));

  cc_wpool_set_num_workers(pool, -2);
  BOOST_CHECK_EQUAL(cc_wpool_get_num_workers(pool), 0);

  cc_wpool_begin(pool, -1);
  BOOST_CHECK_EQUAL(cc_wpool_get_num_workers(pool), 0);
  cc_wpool_end(pool);

  cc_wpool_begin(pool, 1);
  BOOST_CHECK_EQUAL(cc_wpool_get_num_workers(pool), 0);
  cc_wpool_end(pool);

  cc_wpool_destruct(pool);
}

BOOST_AUTO_TEST_CASE(cc_wpool_ignores_null_and_excess_starts)
{
  cc_wpool * pool = cc_wpool_construct(1);
  int calls = 0;

  BOOST_CHECK(!cc_wpool_try_begin(pool, 2));
  BOOST_REQUIRE(cc_wpool_try_begin(pool, 1));

  cc_wpool_start_worker(pool, NULL, NULL);
  cc_wpool_start_worker(pool, wpool_test_callback, &calls);
  cc_wpool_start_worker(pool, wpool_test_callback, &calls);
  cc_wpool_end(pool);

  cc_wpool_wait_all(pool);
  BOOST_CHECK_EQUAL(calls, 1);

  BOOST_REQUIRE(cc_wpool_try_begin(pool, 1));
  cc_wpool_end(pool);

  cc_wpool_destruct(pool);
}

typedef struct {
  cc_mutex * mutex;
  cc_condvar * condvar;
  SbBool entered;
  SbBool release;
} wpool_test_gate;

static void
wpool_test_gate_construct(wpool_test_gate * gate)
{
  gate->mutex = cc_mutex_construct();
  gate->condvar = cc_condvar_construct();
  gate->entered = FALSE;
  gate->release = FALSE;
}

static void
wpool_test_gate_destruct(wpool_test_gate * gate)
{
  cc_condvar_destruct(gate->condvar);
  cc_mutex_destruct(gate->mutex);
}

static void
wpool_test_blocking_callback(void * closure)
{
  wpool_test_gate * gate = static_cast<wpool_test_gate *>(closure);
  cc_mutex_lock(gate->mutex);
  gate->entered = TRUE;
  cc_condvar_wake_all(gate->condvar);
  while (!gate->release) {
    cc_condvar_wait(gate->condvar, gate->mutex);
  }
  cc_mutex_unlock(gate->mutex);
}

static void
wpool_test_wait_until_entered(wpool_test_gate * gate)
{
  cc_mutex_lock(gate->mutex);
  while (!gate->entered) {
    cc_condvar_wait(gate->condvar, gate->mutex);
  }
  cc_mutex_unlock(gate->mutex);
}

static void
wpool_test_release(wpool_test_gate * gate)
{
  cc_mutex_lock(gate->mutex);
  gate->release = TRUE;
  cc_condvar_wake_all(gate->condvar);
  cc_mutex_unlock(gate->mutex);
}

static void
wpool_test_atomic_callback(void * closure)
{
  std::atomic<int> * calls = static_cast<std::atomic<int> *>(closure);
  calls->fetch_add(1, std::memory_order_relaxed);
}

typedef struct {
  cc_wpool * pool;
  std::atomic<int> * jobcalls;
  std::atomic<int> * completed;
} wpool_test_controller_data;

static void *
wpool_test_controller(void * closure)
{
  wpool_test_controller_data * data =
    static_cast<wpool_test_controller_data *>(closure);
  cc_wpool_begin(data->pool, 1);
  cc_wpool_start_worker(data->pool, wpool_test_atomic_callback,
                        data->jobcalls);
  cc_wpool_end(data->pool);
  data->completed->fetch_add(1, std::memory_order_release);
  return NULL;
}

static SbBool
wpool_test_wait_for_value(std::atomic<int> * value, int expected)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (value->load(std::memory_order_acquire) == expected) return TRUE;
    std::this_thread::yield();
  }
  return value->load(std::memory_order_acquire) == expected;
}

static SbBool
wpool_test_wait_for_waiters(cc_wpool * pool, unsigned int expected)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    cc_mutex_lock(pool->mutex);
    const unsigned int numwaiters = pool->numwaiters;
    cc_mutex_unlock(pool->mutex);
    if (numwaiters == expected) return TRUE;
    std::this_thread::yield();
  }
  return FALSE;
}

BOOST_AUTO_TEST_CASE(cc_wpool_wakes_each_waiting_controller)
{
  cc_wpool * pool = cc_wpool_construct(1);
  wpool_test_gate gate;
  wpool_test_gate_construct(&gate);

  BOOST_REQUIRE(cc_wpool_try_begin(pool, 1));
  cc_wpool_start_worker(pool, wpool_test_blocking_callback, &gate);
  cc_wpool_end(pool);
  wpool_test_wait_until_entered(&gate);

  std::atomic<int> jobcalls(0);
  std::atomic<int> completed(0);
  wpool_test_controller_data data = { pool, &jobcalls, &completed };
  cc_thread * first = cc_thread_construct(wpool_test_controller, &data);
  cc_thread * second = cc_thread_construct(wpool_test_controller, &data);
  BOOST_REQUIRE(first != NULL);
  BOOST_REQUIRE(second != NULL);

  const SbBool bothwaiting = wpool_test_wait_for_waiters(pool, 2);
  wpool_test_release(&gate);
  const SbBool completedwithoutrescue =
    bothwaiting && wpool_test_wait_for_value(&completed, 2);

  if (!completedwithoutrescue) {
    cc_mutex_lock(pool->mutex);
    cc_condvar_wake_all(pool->waitcond);
    cc_mutex_unlock(pool->mutex);
    (void) wpool_test_wait_for_value(&completed, 2);
  }

  cc_thread_join(first, NULL);
  cc_thread_join(second, NULL);
  cc_thread_destruct(first);
  cc_thread_destruct(second);
  cc_wpool_wait_all(pool);

  BOOST_CHECK(bothwaiting);
  BOOST_CHECK(completedwithoutrescue);
  BOOST_CHECK_EQUAL(completed.load(std::memory_order_acquire), 2);
  BOOST_CHECK_EQUAL(jobcalls.load(std::memory_order_relaxed), 2);

  wpool_test_gate_destruct(&gate);
  cc_wpool_destruct(pool);
}

#endif // COIN_TEST_SUITE
