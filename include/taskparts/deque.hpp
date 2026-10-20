#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#ifndef TASKPARTS_DEQUE_CAPACITY
#define TASKPARTS_DEQUE_CAPACITY 1024
#endif

#ifndef TASKPARTS_CACHE_LINE_SZB
#define TASKPARTS_CACHE_LINE_SZB 64
#endif

namespace taskparts {

/*---------------------------------------------------------------------*/
/* Fixed-capacity Chase-Lev work-stealing deque
 *
 * The owner pushes and pops at the bottom; thieves steal from the
 * top. Memory orderings follow Le et al., "Correct and Efficient
 * Work-Stealing for Weak Memory Models" (PPoPP'13).
 *
 * Layout follows parlaylib's deque: top and bottom share one cache
 * line, so a thief reads both with one miss, and each slot has a
 * cache line of its own, so the owner pushing and popping at the
 * bottom doesn't invalidate the line that thieves read at the top.
 */

template <typename T>
class deque {
  static constexpr std::int64_t capacity = TASKPARTS_DEQUE_CAPACITY;

  struct alignas(TASKPARTS_CACHE_LINE_SZB) slot {
    std::atomic<T*> item;
  };

  alignas(TASKPARTS_CACHE_LINE_SZB) std::atomic<std::int64_t> top{0};
  std::atomic<std::int64_t> bottom{0};
  slot items[capacity];

public:

  void push(T* x) {
    auto b = bottom.load(std::memory_order_relaxed);
    auto t = top.load(std::memory_order_acquire);
    if (b - t >= capacity) {
      std::fprintf(stderr, "taskparts: deque overflow; raise TASKPARTS_DEQUE_CAPACITY\n");
      std::abort();
    }
    items[b % capacity].item.store(x, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    bottom.store(b + 1, std::memory_order_relaxed);
  }

  T* pop() {
    auto b = bottom.load(std::memory_order_relaxed) - 1;
    bottom.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto t = top.load(std::memory_order_relaxed);
    if (t > b) { // empty
      bottom.store(b + 1, std::memory_order_relaxed);
      return nullptr;
    }
    T* x = items[b % capacity].item.load(std::memory_order_relaxed);
    if (t == b) { // last item: race against thieves
      if (! top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                        std::memory_order_relaxed)) {
        x = nullptr;
      }
      bottom.store(b + 1, std::memory_order_relaxed);
    }
    return x;
  }

  T* steal() {
    auto t = top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto b = bottom.load(std::memory_order_acquire);
    if (t >= b) {
      return nullptr;
    }
    T* x = items[t % capacity].item.load(std::memory_order_relaxed);
    if (! top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                      std::memory_order_relaxed)) {
      return nullptr;
    }
    return x;
  }

};

} // namespace taskparts
