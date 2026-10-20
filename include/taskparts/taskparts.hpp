#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include "context.hpp"
#include "deque.hpp"

#ifndef TASKPARTS_STACK_SZB
#define TASKPARTS_STACK_SZB (1 << 20)
#endif

/*---------------------------------------------------------------------*/
/* A minimal work-stealing scheduler for binary fork join
 *
 * pardo(f, g) pushes g on the deque of the calling worker, then runs
 * f. If g is still there when f returns, the worker pops g and runs it
 * as an ordinary function call. Otherwise, a thief stole g and is
 * running it on a fresh fiber (i.e., a heap-allocated call stack). In
 * that case, whichever of the two branches finishes first goes back to
 * its scheduler to steal work, and whichever finishes second resumes
 * the continuation of the pardo immediately, wherever that continuation
 * happens to be. The continuation is resumed by restoring a context
 * saved in the join point, which lives in the stack frame of the pardo.
 *
 * A worker returns to its scheduler (on the worker thread's own stack)
 * whenever it arrives at a join point via a stolen branch, and only
 * then is the join counter decremented. This way, a computation that is
 * being resumed by another worker never shares a stack with a worker
 * that is still leaving it.
 */

namespace taskparts {

/*---------------------------------------------------------------------*/
/* Join points */

struct join_point {
  // runs the branch g of the pardo
  void (*run)(join_point*);
  // number of branches yet to arrive (relevant only once g is stolen)
  std::atomic<int> nb_unfinished{2};
  // the continuation of the pardo, saved when f finishes before g
  context::context_type continuation;
};

template <typename G>
struct join_point_of : join_point {
  G& g;
  join_point_of(G& g) : g(g) {
    run = [] (join_point* jp) { static_cast<join_point_of*>(jp)->g(); };
  }
};

/*---------------------------------------------------------------------*/
/* Fibers: each stolen branch runs on its own call stack */

struct fiber {
  static constexpr std::size_t stack_szb = TASKPARTS_STACK_SZB;

  join_point* jp;
  char* stack;

  fiber(join_point* jp) : jp(jp) {
    void* p = mmap(nullptr, stack_szb, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
      std::perror("taskparts: failed to allocate fiber stack");
      std::abort();
    }
    // guard page, to fault on stack overflow
    mprotect(p, sysconf(_SC_PAGESIZE), PROT_NONE);
    stack = static_cast<char*>(p);
  }

  ~fiber() {
    munmap(stack, stack_szb);
  }
};

/*---------------------------------------------------------------------*/
/* Workers */

struct alignas(128) worker {
  std::size_t id;
  std::uint64_t rng;
  deque<join_point> dq;
  // where the worker thread runs its scheduler loop
  context::context_type scheduler_ctx;
  // set immediately before switching to scheduler_ctx: the join point
  // at which the worker just arrived, and the fiber that just finished
  join_point* arriving = nullptr;
  fiber* finished = nullptr;

  worker(std::size_t id) : id(id), rng(0x9E3779B97F4A7C15ull * (id + 1)) { }
};

namespace detail {

inline std::vector<std::unique_ptr<worker>> workers;
inline std::atomic<bool> done{false};
inline thread_local worker* tls_worker = nullptr;

// A fiber can migrate between worker threads at any pardo, so the
// compiler must not cache the address of a thread-local variable across
// one. The opaque call and empty asm keep it from doing so.
[[gnu::noinline]] inline
worker* current_worker() {
  worker* w = tls_worker;
  asm volatile("" : "+r"(w));
  return w;
}

inline
std::size_t random_other_worker(worker* w) {
  w->rng ^= w->rng << 13;
  w->rng ^= w->rng >> 7;
  w->rng ^= w->rng << 17;
  auto v = w->rng % (workers.size() - 1);
  return (v >= w->id) ? v + 1 : v;
}

inline
join_point* find_work(worker* w) {
  // the deque of a worker in its scheduler loop is empty, except for
  // worker 0 at the very start, when its deque holds the root
  if (auto jp = w->dq.pop()) {
    return jp;
  }
  if (workers.size() == 1) {
    return nullptr;
  }
  return workers[random_other_worker(w)]->dq.steal();
}

[[noreturn]] inline
void fiber_main(void* arg) {
  auto f = static_cast<fiber*>(arg);
  f->jp->run(f->jp);
  auto w = current_worker(); // not necessarily the worker that started f
  w->finished = f;
  w->arriving = f->jp;
  context::throw_to(w->scheduler_ctx);
}

inline
void scheduler_loop(worker* w) {
  std::size_t nb_failed_steals = 0;
  while (true) {
    // we're back on the scheduler stack, so it's safe to free the
    // previous fiber and to let another worker resume the continuation
    if (w->finished != nullptr) {
      delete std::exchange(w->finished, nullptr);
    }
    if (auto jp = std::exchange(w->arriving, nullptr)) {
      if (jp->nb_unfinished.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // arrived second: resume the continuation right away
        context::swap(w->scheduler_ctx, jp->continuation);
        continue;
      }
    }
    if (done.load(std::memory_order_acquire)) {
      return;
    }
    auto jp = find_work(w);
    if (jp == nullptr) {
      if (++nb_failed_steals % 64 == 0) {
        std::this_thread::yield();
      }
      continue;
    }
    nb_failed_steals = 0;
    auto f = new fiber(jp);
    context::context_type ctx;
    context::prepare(ctx, f->stack, fiber::stack_szb, fiber_main);
    context::swap(w->scheduler_ctx, ctx, f);
  }
}

inline
std::size_t default_nb_workers() {
  if (auto s = std::getenv("TASKPARTS_NUM_WORKERS")) {
    return std::max(1, std::stoi(s));
  }
  return std::max(1u, std::thread::hardware_concurrency());
}

} // namespace detail

/*---------------------------------------------------------------------*/
/* Interface */

// Runs f() on a fresh scheduler with nb_workers worker threads, one of
// which is the calling thread. Returns after f() returns.
template <typename F>
void launch(F&& f, std::size_t nb_workers = detail::default_nb_workers()) {
  using namespace detail;
  if (current_worker() != nullptr) { // already inside the scheduler
    f();
    return;
  }
  for (std::size_t i = 0; i < nb_workers; i++) {
    workers.push_back(std::make_unique<worker>(i));
  }
  done.store(false);
  auto root = [&] {
    f();
    done.store(true, std::memory_order_release);
  };
  // nobody waits on the root's join point: only one branch ever arrives
  join_point_of<decltype(root)> root_jp(root);
  workers[0]->dq.push(&root_jp);
  std::vector<std::thread> threads;
  for (std::size_t i = 1; i < nb_workers; i++) {
    threads.emplace_back([i] {
      tls_worker = workers[i].get();
      scheduler_loop(workers[i].get());
    });
  }
  tls_worker = workers[0].get();
  scheduler_loop(workers[0].get());
  tls_worker = nullptr;
  for (auto& t : threads) {
    t.join();
  }
  workers.clear();
}

// Runs f() and g() in parallel, returning after both return. Neither may
// throw. Called outside of launch(), it launches a scheduler first.
template <typename F, typename G>
void pardo(F&& f, G&& g) {
  using namespace detail;
  auto w = current_worker();
  if (w == nullptr) {
    launch([&] { pardo(f, g); });
    return;
  }
  join_point_of<std::remove_reference_t<G>> jp(g);
  w->dq.push(&jp);
  f();
  w = current_worker(); // f() may have finished on another worker
  if (w->dq.pop() != nullptr) { // g was not stolen
    g();
    return;
  }
  // g was stolen: suspend here; resumed by whichever branch arrives second
  w->arriving = &jp;
  context::swap(jp.continuation, w->scheduler_ctx);
}

inline
std::size_t nb_workers() {
  return detail::workers.size();
}

// The id of the calling worker, which may change across a pardo.
inline
std::size_t worker_id() {
  auto w = detail::current_worker();
  return (w == nullptr) ? 0 : w->id;
}

} // namespace taskparts
