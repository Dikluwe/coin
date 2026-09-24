/*
 * Worker concurrency contract exercised through the public C thread API.
 * Gates hold callbacks at specific points so a missed lock transition is
 * reproducible without relying on scheduler timing.
 */

#include "CoinTest.h"

#include <Inventor/C/threads/sched.h>
#include <Inventor/C/threads/worker.h>
#include <Inventor/C/threads/wpool.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

const std::chrono::seconds worker_test_timeout(5);

struct WorkerGate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered;
  bool released;

  WorkerGate() : entered(false), released(false) { }

  void stop_here()
  {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    changed.wait(lock, [this] { return released; });
  }

  bool wait_until_entered()
  {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, worker_test_timeout,
                            [this] { return entered; });
  }

  void open()
  {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

bool
wait_for_flag(const std::atomic<bool> & flag,
              std::chrono::steady_clock::duration timeout = worker_test_timeout)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (flag.load(std::memory_order_acquire)) return true;
    std::this_thread::yield();
  }
  return flag.load(std::memory_order_acquire);
}

struct GatedWork {
  WorkerGate gate;
  std::atomic<int> calls;

  GatedWork() : calls(0) { }
};

void
gated_work(void * closure)
{
  GatedWork * work = static_cast<GatedWork *>(closure);
  work->calls.fetch_add(1, std::memory_order_relaxed);
  work->gate.stop_here();
}

void
count_work(void * closure)
{
  std::atomic<int> * calls = static_cast<std::atomic<int> *>(closure);
  calls->fetch_add(1, std::memory_order_relaxed);
}

struct BusyProbe {
  cc_worker * worker;
  std::atomic<int> work_calls;
  std::atomic<int> idle_calls;
  std::atomic<bool> busy_in_work;
  std::atomic<bool> idle_query_returned;

  explicit BusyProbe(cc_worker * value)
    : worker(value), work_calls(0), idle_calls(0),
      busy_in_work(false), idle_query_returned(false) { }
};

void
busy_probe_work(void * closure)
{
  BusyProbe * probe = static_cast<BusyProbe *>(closure);
  probe->busy_in_work.store(cc_worker_is_busy(probe->worker) != FALSE,
                            std::memory_order_release);
  probe->work_calls.fetch_add(1, std::memory_order_relaxed);
}

void
busy_probe_idle(cc_worker * worker, void * closure)
{
  BusyProbe * probe = static_cast<BusyProbe *>(closure);
  (void) cc_worker_is_busy(worker);
  probe->idle_query_returned.store(true, std::memory_order_release);
  probe->idle_calls.fetch_add(1, std::memory_order_relaxed);
}

struct ExternalLockWork {
  std::mutex * external;
  std::atomic<bool> entered;
  std::atomic<int> calls;

  explicit ExternalLockWork(std::mutex * value)
    : external(value), entered(false), calls(0) { }
};

void
external_lock_work(void * closure)
{
  ExternalLockWork * work = static_cast<ExternalLockWork *>(closure);
  work->entered.store(true, std::memory_order_release);
  std::lock_guard<std::mutex> lock(*work->external);
  work->calls.fetch_add(1, std::memory_order_relaxed);
}

struct IdleClosure {
  WorkerGate gate;
  std::atomic<int> calls;
  std::atomic<int> uses_after_retirement;
  std::atomic<bool> retired;

  IdleClosure() : calls(0), uses_after_retirement(0), retired(false) { }
};

void
gated_idle(cc_worker *, void * closure)
{
  IdleClosure * idle = static_cast<IdleClosure *>(closure);
  idle->calls.fetch_add(1, std::memory_order_relaxed);
  idle->gate.stop_here();
  if (idle->retired.load(std::memory_order_acquire)) {
    idle->uses_after_retirement.fetch_add(1, std::memory_order_relaxed);
  }
}

void
count_idle(cc_worker *, void * closure)
{
  count_work(closure);
}

} // namespace

BOOST_AUTO_TEST_CASE(cc_worker_busy_query_works_in_both_callbacks)
{
  cc_worker * worker = cc_worker_construct();
  BusyProbe probe(worker);
  cc_worker_set_idle_callback(worker, busy_probe_idle, &probe);

  BOOST_REQUIRE(cc_worker_start(worker, busy_probe_work, &probe));
  cc_worker_wait(worker);

  BOOST_CHECK_EQUAL(probe.work_calls.load(), 1);
  BOOST_CHECK_EQUAL(probe.idle_calls.load(), 1);
  BOOST_CHECK(probe.busy_in_work.load(std::memory_order_acquire));
  BOOST_CHECK(probe.idle_query_returned.load(std::memory_order_acquire));
  BOOST_CHECK(!cc_worker_is_busy(worker));
  cc_worker_destruct(worker);
}

BOOST_AUTO_TEST_CASE(cc_worker_concurrent_start_cannot_replace_active_task)
{
  cc_worker * worker = cc_worker_construct();
  GatedWork first;
  std::atomic<int> second_calls(0);
  BOOST_REQUIRE(cc_worker_start(worker, gated_work, &first));
  const bool entered = first.gate.wait_until_entered();

  std::atomic<bool> attempted(false);
  std::atomic<bool> returned(false);
  std::atomic<bool> accepted(false);
  std::thread contender([&] {
    attempted.store(true, std::memory_order_release);
    accepted.store(cc_worker_start(worker, count_work, &second_calls) != FALSE,
                   std::memory_order_release);
    returned.store(true, std::memory_order_release);
  });

  const bool attempt_started = wait_for_flag(attempted);
  const bool returned_while_active =
    attempt_started && wait_for_flag(returned);
  const bool busy_while_active = cc_worker_is_busy(worker) != FALSE;

  first.gate.open();
  contender.join();
  cc_worker_wait(worker);

  BOOST_CHECK(entered);
  BOOST_CHECK(attempt_started);
  BOOST_CHECK(returned_while_active);
  BOOST_CHECK(busy_while_active);
  BOOST_CHECK(!accepted.load(std::memory_order_acquire));
  BOOST_CHECK_EQUAL(first.calls.load(), 1);
  BOOST_CHECK_EQUAL(second_calls.load(), 0);
  cc_worker_destruct(worker);
}

BOOST_AUTO_TEST_CASE(cc_worker_setter_does_not_wait_on_external_work_lock)
{
  cc_worker * worker = cc_worker_construct();
  std::mutex external;
  external.lock();
  ExternalLockWork work(&external);
  BOOST_REQUIRE(cc_worker_start(worker, external_lock_work, &work));
  const bool work_entered = wait_for_flag(work.entered);

  std::atomic<bool> setter_started(false);
  std::atomic<bool> setter_returned(false);
  std::thread setter([&] {
    setter_started.store(true, std::memory_order_release);
    cc_worker_set_idle_callback(worker, NULL, NULL);
    setter_returned.store(true, std::memory_order_release);
  });

  const bool started = wait_for_flag(setter_started);
  const bool returned_while_external_locked =
    started && wait_for_flag(setter_returned);

  external.unlock();
  setter.join();
  cc_worker_wait(worker);

  BOOST_CHECK(work_entered);
  BOOST_CHECK(started);
  BOOST_CHECK(returned_while_external_locked);
  BOOST_CHECK_EQUAL(work.calls.load(), 1);
  cc_worker_destruct(worker);
}

BOOST_AUTO_TEST_CASE(cc_worker_idle_setter_retires_old_closure_before_return)
{
  cc_worker * worker = cc_worker_construct();
  IdleClosure old_idle;
  std::atomic<int> new_idle_calls(0);
  std::atomic<int> work_calls(0);
  cc_worker_set_idle_callback(worker, gated_idle, &old_idle);
  BOOST_REQUIRE(cc_worker_start(worker, count_work, &work_calls));
  const bool old_entered = old_idle.gate.wait_until_entered();

  std::atomic<bool> setter_started(false);
  std::atomic<bool> setter_returned(false);
  std::thread setter([&] {
    setter_started.store(true, std::memory_order_release);
    cc_worker_set_idle_callback(worker, count_idle, &new_idle_calls);
    setter_returned.store(true, std::memory_order_release);
  });

  const bool started = wait_for_flag(setter_started);
  const bool returned_while_old_active =
    started && wait_for_flag(setter_returned, std::chrono::milliseconds(250));

  old_idle.gate.open();
  setter.join();
  old_idle.retired.store(true, std::memory_order_release);
  cc_worker_wait(worker);

  BOOST_CHECK(old_entered);
  BOOST_CHECK(started);
  BOOST_CHECK(!returned_while_old_active);
  BOOST_CHECK_EQUAL(old_idle.calls.load(), 1);
  BOOST_CHECK_EQUAL(old_idle.uses_after_retirement.load(), 0);
  BOOST_CHECK_EQUAL(work_calls.load(), 1);

  BOOST_REQUIRE(cc_worker_start(worker, count_work, &work_calls));
  cc_worker_wait(worker);
  BOOST_CHECK_EQUAL(work_calls.load(), 2);
  BOOST_CHECK_EQUAL(old_idle.calls.load(), 1);
  BOOST_CHECK_EQUAL(new_idle_calls.load(), 1);
  cc_worker_destruct(worker);
}

BOOST_AUTO_TEST_CASE(cc_worker_wait_and_destruct_drain_accepted_task)
{
  for (int pass = 0; pass < 2; ++pass) {
    cc_worker * worker = cc_worker_construct();
    GatedWork work;
    BOOST_REQUIRE(cc_worker_start(worker, gated_work, &work));
    const bool entered = work.gate.wait_until_entered();

    std::atomic<bool> completed(false);
    std::thread drainer([&] {
      if (pass == 0) cc_worker_wait(worker);
      else cc_worker_destruct(worker);
      completed.store(true, std::memory_order_release);
    });

    const bool completed_before_release =
      wait_for_flag(completed, std::chrono::milliseconds(250));
    work.gate.open();
    drainer.join();

    BOOST_CHECK(entered);
    BOOST_CHECK(!completed_before_release);
    BOOST_CHECK(completed.load(std::memory_order_acquire));
    BOOST_CHECK_EQUAL(work.calls.load(), 1);
    if (pass == 0) cc_worker_destruct(worker);
  }
}

BOOST_AUTO_TEST_CASE(cc_wpool_repeated_dispatch_drains_all_jobs)
{
  cc_wpool * pool = cc_wpool_construct(1);
  std::atomic<int> calls(0);
  for (int i = 0; i < 32; ++i) {
    cc_wpool_begin(pool, 1);
    cc_wpool_start_worker(pool, count_work, &calls);
    cc_wpool_end(pool);
  }
  cc_wpool_wait_all(pool);
  BOOST_CHECK_EQUAL(calls.load(), 32);
  cc_wpool_destruct(pool);
}


BOOST_AUTO_TEST_CASE(cc_sched_repeated_dispatch_drains_all_jobs)
{
  cc_sched * sched = cc_sched_construct(1);
  std::atomic<int> calls(0);
  for (int i = 0; i < 32; ++i) {
    const uint32_t id = cc_sched_schedule(sched, count_work, &calls, 1.0f);
    BOOST_CHECK(id != 0u);
  }
  cc_sched_wait_all(sched);
  BOOST_CHECK_EQUAL(calls.load(), 32);
  cc_sched_destruct(sched);
}
