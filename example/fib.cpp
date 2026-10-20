#include <chrono>
#include <cstdio>
#include <cstdlib>

#include <taskparts/taskparts.hpp>

long fib_seq(long n) {
  return (n < 2) ? n : fib_seq(n - 1) + fib_seq(n - 2);
}

long fib(long n) {
  if (n <= 20) {
    return fib_seq(n);
  }
  long a, b;
  taskparts::pardo([&] { a = fib(n - 1); }, [&] { b = fib(n - 2); });
  return a + b;
}

int main(int argc, char** argv) {
  long n = (argc > 1) ? std::atol(argv[1]) : 40;
  long r = 0;
  auto start = std::chrono::steady_clock::now();
  taskparts::launch([&] { r = fib(n); });
  auto end = std::chrono::steady_clock::now();
  std::printf("fib(%ld) = %ld\n", n, r);
  std::printf("time %.3fs\n", std::chrono::duration<double>(end - start).count());
  return 0;
}
