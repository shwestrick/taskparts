#!/usr/bin/env python3
# Builds each PBBS benchmark three times, once per scheduler:
#   <impl>/<bench>.parlay, <impl>/<bench>.taskparts-main, <impl>/<bench>.taskparts-minimal
# Each binary is compiled as one translation unit (mainline taskparts
# defines globals in its headers), with main() running inside the
# taskparts scheduler.
#
# usage: build.py WORK_DIR [benchmark ...]

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

from common import BENCHMARKS, VARIANTS, compile_flags, jemalloc_flags, makefile_vars

UNITY = "_taskparts_unity.C"


def write_unity(impl_dir):
    bench, objs, linked = makefile_vars(impl_dir)
    lines = ['#include "parlay/primitives.h"', '#include "parlay/parallel.h"',
             "#include <iostream>"]
    if not linked:
        lines.append(f'#include "{bench}.h"')
    lines += ["#define main pbbs_main", f'#include "../bench/{bench}Time.C"', "#undef main"]
    for o in objs:
        lines.append(f'#include "{o[:-2]}.C"')
    lines += [
        # pbbs_main's return value is meaningless when it falls off its end
        "int main(int argc, char* argv[]) {",
        "#ifdef PARLAY_TASKPARTS",
        "  parlay::internal::taskparts_run([&] { pbbs_main(argc, argv); });",
        "#else",
        "  pbbs_main(argc, argv);",
        "#endif",
        "  return 0;",
        "}",
    ]
    with open(os.path.join(impl_dir, UNITY), "w") as f:
        f.write("\n".join(lines) + "\n")
    return bench


def build(work, bench_path, variant):
    impl_dir = os.path.join(work, "pbbsbench", "benchmarks", bench_path)
    bench, _, _ = makefile_vars(impl_dir)
    out = f"{bench}.{variant}"
    cmd = (["clang++"] + compile_flags(variant, work) + ["-o", out, UNITY, "-ldl"]
           + jemalloc_flags())
    r = subprocess.run(cmd, cwd=impl_dir, capture_output=True, text=True)
    status = "ok" if r.returncode == 0 else "FAILED"
    print(f"{status:6} {bench_path} [{variant}]", flush=True)
    if r.returncode != 0:
        print(" ".join(cmd))
        print(r.stderr[-3000:])
    return r.returncode == 0


def main():
    work = os.path.abspath(sys.argv[1])
    benches = sys.argv[2:] or BENCHMARKS
    for b in benches:
        write_unity(os.path.join(work, "pbbsbench", "benchmarks", b))
    jobs = [(b, v) for b in benches for v in VARIANTS]
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        results = list(ex.map(lambda bv: build(work, *bv), jobs))
    failed = [bv for bv, ok in zip(jobs, results) if not ok]
    print(f"{len(jobs) - len(failed)}/{len(jobs)} built")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
