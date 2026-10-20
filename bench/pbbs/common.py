import os
import re
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
TASKPARTS = os.path.abspath(os.path.join(HERE, "..", ".."))

# The parallel benchmarks that pbbsbench's `runall` runs by default
# (i.e., not the extended set), one implementation per problem.
BENCHMARKS = [
    "integerSort/parallelRadixSort",
    "comparisonSort/sampleSort",
    "removeDuplicates/parlayhash",
    "histogram/parallel",
    "wordCounts/histogram",
    "invertedIndex/parallel",
    "suffixArray/parallelRange",
    "longestRepeatedSubstring/doubling",
    "classify/decisionTree",
    "minSpanningForest/parallelFilterKruskal",
    "spanningForest/ndST",
    "breadthFirstSearch/backForwardBFS",
    "maximalMatching/incrementalMatching",
    "maximalIndependentSet/incrementalMIS",
    "nearestNeighbors/octTree",
    "rayCast/kdTree",
    "convexHull/quickHull",
    "delaunayTriangulation/incrementalDelaunay",
    "delaunayRefine/incrementalRefine",
    # "rangeQuery2d/parallelPlaneSweep",  # needs pbbsbench's PAM submodule
    "nBody/parallelCK",
]

VARIANTS = ["parlay", "taskparts-main", "taskparts-minimal"]


def jemalloc_flags():
    if shutil.which("jemalloc-config") is None:
        return []
    libdir = subprocess.check_output(["jemalloc-config", "--libdir"], text=True).strip()
    return [f"-L{libdir}", f"-Wl,-rpath,{libdir}", "-ljemalloc"]


def compile_flags(variant, work):
    # identical for all variants, except for the choice of scheduler
    # -fno-strict-return: main() is renamed to pbbs_main() (see build.py),
    # and PBBS's mains rely on main's implicit `return 0`
    common = ["-std=c++17", "-O3", "-DNDEBUG", "-fno-stack-protector", "-fno-strict-return",
              "-I", ".", "-pthread", "-w"]
    if variant == "parlay":
        return common + ["-DHOMEGROWN"]
    if variant == "taskparts-minimal":
        return common + ["-DPARLAY_TASKPARTS", "-I", os.path.join(TASKPARTS, "include")]
    if variant == "taskparts-main":
        arch = "-DTASKPARTS_ARM64" if os.uname().machine == "arm64" else "-DTASKPARTS_X64"
        plat = "-DTASKPARTS_DARWIN" if os.uname().sysname == "Darwin" else "-DTASKPARTS_POSIX"
        return common + ["-DPARLAY_TASKPARTS", "-DTASKPARTS_MAINLINE", plat, arch,
                         "-I", os.path.join(work, "taskparts-main", "include")]
    raise ValueError(variant)


def env_for(variant, p):
    env = dict(os.environ)
    env["PARLAY_NUM_THREADS"] = str(p)
    env["TASKPARTS_NUM_WORKERS"] = str(p)
    # mainline taskparts refuses to start without it; only used for its
    # heartbeat threshold, which these benchmarks don't use
    env.setdefault("TASKPARTS_CPU_BASE_FREQUENCY_KHZ", "4000000")
    return env


def makefile_vars(impl_dir):
    src = open(os.path.join(impl_dir, "Makefile")).read()
    bench = re.search(r"^BENCH\s*=\s*(\S+)", src, re.M).group(1)
    objs = re.search(r"^OBJS\s*=\s*(.*)$", src, re.M)
    objs = objs.group(1).split() if objs else []
    linked = "MakeBenchLink" in src
    return bench, objs, linked


def binary(work, bench_path, variant):
    impl_dir = os.path.join(work, "pbbsbench", "benchmarks", bench_path)
    bench, _, _ = makefile_vars(impl_dir)
    return os.path.join(impl_dir, f"{bench}.{variant}")


def test_inputs(work, bench_path):
    """Returns (bnchmrk, checkProgram, dataDir, tests) from bench/testInputs."""
    group = bench_path.split("/")[0]
    path = os.path.join(work, "pbbsbench", "benchmarks", group, "bench", "testInputs")
    src = open(path).read()
    src = src[:src.index("import sys")]
    scope = {}
    exec(src, scope)
    return scope["bnchmrk"], scope["checkProgram"], scope["dataDir"], scope["tests"]
