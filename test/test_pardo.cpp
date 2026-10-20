#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <taskparts/taskparts.hpp>

using taskparts::pardo;

static int nb_failures = 0;

#define CHECK(cond) do {                                          \
    if (! (cond)) {                                               \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n",           \
                   __FILE__, __LINE__, #cond);                    \
      nb_failures++;                                              \
    }                                                             \
  } while (0)

long fib_seq(long n) {
  return (n < 2) ? n : fib_seq(n - 1) + fib_seq(n - 2);
}

// no grain size: maximizes the number of steals and migrations
long fib(long n) {
  if (n < 2) {
    return n;
  }
  long a, b;
  pardo([&] { a = fib(n - 1); }, [&] { b = fib(n - 2); });
  return a + b;
}

long sum(const std::vector<long>& xs, size_t lo, size_t hi) {
  if (hi - lo == 1) {
    return xs[lo];
  }
  auto mid = (lo + hi) / 2;
  long a, b;
  pardo([&] { a = sum(xs, lo, mid); }, [&] { b = sum(xs, mid, hi); });
  return a + b;
}

// Keeps many values live across the pardo, in callee-saved integer and
// floating-point registers, to check that the continuation is restored
// properly no matter which worker resumes it.
std::atomic<long> nb_migrations{0};

double live_across(long depth, long x) {
  if (depth <= 0) {
    return (double)x;
  }
  long i0 = x * 3, i1 = x + 7, i2 = x ^ 0x55, i3 = x * x, i4 = x - 11;
  double d0 = x * 0.5, d1 = x * 1.25, d2 = x + 0.125, d3 = x * 2.0;
  double l, r;
  auto id_before = taskparts::worker_id();
  // imbalanced branches, so that both orders of arrival happen
  pardo([&] { l = live_across(depth - 1, i0 % 1000); },
        [&] { r = live_across(depth - 1 - (x & 1), i1 % 1000); });
  if (taskparts::worker_id() != id_before) {
    nb_migrations++;
  }
  return l + r + (double)(i0 + i1 + i2 + i3 + i4) + d0 + d1 + d2 + d3;
}

double live_across_seq(long depth, long x) {
  if (depth <= 0) {
    return (double)x;
  }
  long i0 = x * 3, i1 = x + 7, i2 = x ^ 0x55, i3 = x * x, i4 = x - 11;
  double d0 = x * 0.5, d1 = x * 1.25, d2 = x + 0.125, d3 = x * 2.0;
  double l = live_across_seq(depth - 1, i0 % 1000);
  double r = live_across_seq(depth - 1 - (x & 1), i1 % 1000);
  return l + r + (double)(i0 + i1 + i2 + i3 + i4) + d0 + d1 + d2 + d3;
}

// unbalanced, deep nesting: a spine of pardos each with a small leaf
long spine(long n) {
  if (n == 0) {
    return 0;
  }
  long a, b;
  pardo([&] { a = fib(8); }, [&] { b = spine(n - 1); });
  return a + b;
}

void run_tests(size_t p) {
  std::printf("P = %zu\n", p);
  taskparts::launch([&] {
    CHECK(taskparts::nb_workers() == p);
    for (long n = 0; n < 25; n++) {
      CHECK(fib(n) == fib_seq(n));
    }
    CHECK(fib(30) == fib_seq(30));

    std::vector<long> xs(1 << 18);
    long expected = 0;
    for (size_t i = 0; i < xs.size(); i++) {
      xs[i] = (long)(i * 2654435761u % 1000);
      expected += xs[i];
    }
    for (int i = 0; i < 5; i++) {
      CHECK(sum(xs, 0, xs.size()) == expected);
    }

    nb_migrations = 0;
    for (long x = 0; x < 20; x++) {
      CHECK(live_across(16, x) == live_across_seq(16, x));
    }
    std::printf("  migrations observed: %ld\n", nb_migrations.load());

    CHECK(spine(2000) == 2000 * fib_seq(8));
  }, p);
}

int main() {
  for (size_t p : {1, 2, 3, 4, 8, 16, 32}) {
    run_tests(p);
  }
  // pardo outside of launch() launches a scheduler of its own
  long a, b;
  pardo([&] { a = fib(20); }, [&] { b = fib(21); });
  CHECK(a == fib_seq(20) && b == fib_seq(21));
  if (nb_failures == 0) {
    std::printf("all tests passed\n");
  }
  return (nb_failures == 0) ? 0 : 1;
}
