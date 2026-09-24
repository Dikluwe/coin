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
  \struct cc_worker common.h Inventor/C/threads/common.h
  \ingroup coin_threads
  \brief The structure for a worker thread.
*/

/*!
  \typedef struct cc_worker cc_worker
  \ingroup coin_threads
  \brief The type definition for the worker thread structure.
*/

#include <Inventor/C/threads/worker.h>

#include <cstdlib>
#include <cassert>

#include <Inventor/C/threads/thread.h>
#include <Inventor/C/threads/mutex.h>
#include <Inventor/C/threads/condvar.h>
#include <Inventor/C/errors/debugerror.h>

#include "threads/workerp.h"
#include "threads/mutexp.h"

/* ********************************************************************** */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * This mutex protects the worker's state only. Neither callback may run
 * while it is held: both are allowed to acquire unrelated scheduler locks.
 */
static void
worker_thread_loop(cc_worker * worker)
{
  cc_mutex_lock(worker->mutex);
  for (;;) {
    while (!worker->haswork && !worker->shutdown) {
      cc_condvar_wait(worker->cond, worker->mutex);
    }
    if (!worker->haswork) break;

    cc_worker_f * workfunc = worker->workfunc;
    void * workclosure = worker->workclosure;
    worker->haswork = FALSE;
    worker->workfunc = NULL;
    worker->workclosure = NULL;
    worker->working = TRUE;
    cc_mutex_unlock(worker->mutex);

    workfunc(workclosure);

    cc_mutex_lock(worker->mutex);
    worker->working = FALSE;
    cc_worker_idle_f * idlecb = worker->idlecb;
    void * idleclosure = worker->idleclosure;
    if (idlecb != NULL) {
      worker->idleactive = TRUE;
      worker->idlecall++;
      cc_mutex_unlock(worker->mutex);

      idlecb(worker, idleclosure);

      cc_mutex_lock(worker->mutex);
      worker->idleactive = FALSE;
    }
    cc_condvar_wake_all(worker->statecond);
  }
  cc_mutex_unlock(worker->mutex);
}

/*
 * Entry point for the thread.
 */
static void *
worker_thread_entry(void * data)
{
  worker_thread_loop((cc_worker*) data);
  return NULL;
}

/*
 * Stop and join the thread after all accepted work and its idle callback.
 * The join mutex serializes callers of wait() and destruct().
 */
static void
worker_stop_thread(cc_worker * worker)
{
  cc_mutex_lock(worker->joinmutex);
  cc_mutex_lock(worker->mutex);
  cc_thread * thread = worker->thread;
  if (thread != NULL) {
    worker->shutdown = TRUE;
    cc_condvar_wake_one(worker->cond);
  }
  cc_mutex_unlock(worker->mutex);

  if (thread != NULL) {
    cc_thread_join(thread, NULL);
    cc_thread_destruct(thread);
    cc_mutex_lock(worker->mutex);
    worker->thread = NULL;
    worker->threadisrunning = FALSE;
    worker->shutdown = FALSE;
    cc_mutex_unlock(worker->mutex);
  }
  cc_mutex_unlock(worker->joinmutex);
}

cc_worker *
cc_worker_construct(void)
{
  cc_worker * worker = (cc_worker*) malloc(sizeof(cc_worker));
  assert(worker);

  worker->mutex = cc_mutex_construct();
  worker->joinmutex = cc_mutex_construct();
  worker->cond = cc_condvar_construct();
  worker->statecond = cc_condvar_construct();
  worker->thread = NULL; /* delay creating thread */
  worker->threadisrunning = FALSE;
  worker->shutdown = FALSE;
  worker->haswork = FALSE;
  worker->working = FALSE;
  worker->idleactive = FALSE;
  worker->idlecall = 0;
  worker->workfunc = NULL;
  worker->workclosure = NULL;
  worker->idlecb = NULL;
  worker->idleclosure = NULL;
  return worker;
}

/*!
  Will wait for the current task to be finished before destructing the worker.
*/
void
cc_worker_destruct(cc_worker * worker)
{
  worker_stop_thread(worker);
  assert(worker->thread == NULL);
  cc_mutex_destruct(worker->mutex);
  cc_mutex_destruct(worker->joinmutex);
  cc_condvar_destruct(worker->cond);
  cc_condvar_destruct(worker->statecond);
  free(worker);
}

/*!
  Start the worker by either waking it from a condvar_wait() or
  creating a new thread if the thread is not running.
*/
SbBool
cc_worker_start(cc_worker * worker, cc_worker_f * workfunc, void * closure)
{
  if (workfunc == NULL) return FALSE;

  cc_mutex_lock(worker->mutex);
  if (worker->shutdown || worker->haswork || worker->working) {
    cc_mutex_unlock(worker->mutex);
    return FALSE;
  }

  /*
   * An idle callback can publish this worker to cc_wpool before it returns.
   * Reserve the next job while that callback finishes, without replacing it.
   */
  worker->workfunc = workfunc;
  worker->workclosure = closure;
  worker->haswork = TRUE;

  if (!worker->threadisrunning) {
    worker->thread = cc_thread_construct(worker_thread_entry, worker);
    if (worker->thread == NULL) {
      worker->haswork = FALSE;
      worker->workfunc = NULL;
      worker->workclosure = NULL;
      cc_mutex_unlock(worker->mutex);
      return FALSE;
    }
    worker->threadisrunning = TRUE;
  }

  cc_condvar_wake_one(worker->cond);
  cc_mutex_unlock(worker->mutex);
  return TRUE;
}

SbBool
cc_worker_is_busy(cc_worker * worker)
{
  cc_mutex_lock(worker->mutex);
  const SbBool busy = worker->haswork || worker->working || worker->idleactive;
  cc_mutex_unlock(worker->mutex);
  return busy;
}

void
cc_worker_wait(cc_worker * worker)
{
  worker_stop_thread(worker);
}

void
cc_worker_set_idle_callback(cc_worker * worker, cc_worker_idle_f * cb,
                            void * closure)
{
  cc_mutex_lock(worker->mutex);
  const SbBool wasactive = worker->idleactive;
  const unsigned long oldcall = worker->idlecall;
  worker->idlecb = cb;
  worker->idleclosure = closure;
  /*
   * A callback already in flight copied the old closure. Wait only for
   * that invocation; a later invocation may use the replacement closure.
   */
  while (wasactive && worker->idleactive && worker->idlecall == oldcall) {
    cc_condvar_wait(worker->statecond, worker->mutex);
  }
  cc_mutex_unlock(worker->mutex);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#ifdef COIN_TEST_SUITE

static void
worker_test_callback(void * closure)
{
  int * calls = static_cast<int *>(closure);
  (*calls)++;
}

BOOST_AUTO_TEST_CASE(cc_worker_rejects_null_and_runs_each_task_once)
{
  cc_worker * worker = cc_worker_construct();
  int calls = 0;

  BOOST_CHECK(!cc_worker_start(worker, NULL, NULL));

  BOOST_CHECK(cc_worker_start(worker, worker_test_callback, &calls));
  cc_worker_wait(worker);
  BOOST_CHECK_EQUAL(calls, 1);

  BOOST_CHECK(cc_worker_start(worker, worker_test_callback, &calls));
  cc_worker_wait(worker);
  BOOST_CHECK_EQUAL(calls, 2);

  cc_worker_destruct(worker);
}

#endif // COIN_TEST_SUITE
