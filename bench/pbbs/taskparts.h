// Parlay scheduler plugin that runs parlay's par_do and parallel_for
// on taskparts. Install as parlay/internal/scheduler_plugins/taskparts.h
// and select with -DPARLAY_TASKPARTS. Define TASKPARTS_MAINLINE to build
// against the mainline taskparts (main branch) instead of the minimal one.
//
// Unlike parlay's own scheduler, taskparts must be running before the
// first par_do, so the whole program must run inside
// parlay::internal::taskparts_run(f).

#ifndef PARLAY_INTERNAL_SCHEDULER_PLUGINS_TASKPARTS_H_
#define PARLAY_INTERNAL_SCHEDULER_PLUGINS_TASKPARTS_H_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <type_traits>
#include <utility>

#ifdef TASKPARTS_MAINLINE
#include <taskparts/defaults.hpp>
#else
#include <taskparts/taskparts.hpp>
#endif

namespace parlay {

// IWYU pragma: private, include "../../parallel.h"

#ifdef TASKPARTS_MAINLINE

inline size_t num_workers() { return taskparts::perworker::nb_workers(); }
inline size_t worker_id() { return taskparts::perworker::my_id(); }

template <typename Lf, typename Rf>
inline void par_do(Lf&& left, Rf&& right, bool) {
  static_assert(std::is_invocable_v<Lf&&>);
  static_assert(std::is_invocable_v<Rf&&>);
  taskparts::fork2join(left, right);
}

namespace internal {
template <typename F>
void taskparts_run(F&& f) {
  taskparts::launch([&] (auto) { f(); });
}
}  // namespace internal

#else

inline size_t num_workers() {
  auto p = taskparts::nb_workers();
  return (p == 0) ? taskparts::detail::default_nb_workers() : p;
}
inline size_t worker_id() { return taskparts::worker_id(); }

template <typename Lf, typename Rf>
inline void par_do(Lf&& left, Rf&& right, bool) {
  static_assert(std::is_invocable_v<Lf&&>);
  static_assert(std::is_invocable_v<Rf&&>);
  taskparts::pardo(left, right);
}

namespace internal {
template <typename F>
void taskparts_run(F&& f) {
  taskparts::launch(f);
}
}  // namespace internal

#endif

// Same loop decomposition as parlay's own scheduler (parlay/scheduler.h),
// so that only the scheduler differs.
namespace internal {

template <typename F>
size_t taskparts_get_granularity(size_t start, size_t end, F& f) {
  size_t done = 0;
  size_t sz = 1;
  unsigned long long int ticks = 0;
  do {
    sz = std::min(sz, end - (start + done));
    auto tstart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < sz; i++) f(start + done + i);
    auto tstop = std::chrono::steady_clock::now();
    ticks = static_cast<unsigned long long int>(std::chrono::duration_cast<
              std::chrono::nanoseconds>(tstop - tstart).count());
    done += sz;
    sz *= 2;
  } while (ticks < 1000 && done < (end - start));
  return done;
}

template <typename F>
void taskparts_parfor(size_t start, size_t end, F& f, size_t granularity) {
  if ((end - start) <= granularity)
    for (size_t i = start; i < end; i++) f(i);
  else {
    size_t n = end - start;
    size_t mid = (start + (9 * (n + 1)) / 16);
    par_do([&] { taskparts_parfor(start, mid, f, granularity); },
           [&] { taskparts_parfor(mid, end, f, granularity); });
  }
}

}  // namespace internal

template <typename F>
inline void parallel_for(size_t start, size_t end, F&& f, long granularity, bool) {
  static_assert(std::is_invocable_v<F&, size_t>);
  if (start + 1 == end) {
    f(start);
  } else if ((end - start) <= static_cast<size_t>(granularity)) {
    for (size_t i = start; i < end; i++) f(i);
  } else if (end > start) {
    size_t g = static_cast<size_t>(granularity);
    if (g == 0) {
      size_t done = internal::taskparts_get_granularity(start, end, f);
      g = std::max(done, (end - start) / (128 * num_workers()));
      start += done;
    }
    internal::taskparts_parfor(start, end, f, g);
  }
}

template <typename... Fs>
void execute_with_scheduler(Fs...) {
  struct Illegal {};
  static_assert((std::is_same_v<Illegal, Fs> && ...),
                "parlay::execute_with_scheduler is not available with taskparts");
}

}  // namespace parlay

#endif  // PARLAY_INTERNAL_SCHEDULER_PLUGINS_TASKPARTS_H_
