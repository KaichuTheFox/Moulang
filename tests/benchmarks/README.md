# Windows pre-ARC benchmarks

Run this from the repository root after building Moulang and installing the pinned LLVM-MinGW toolchain. Use Windows x64 and CPython.

```powershell
python -B tests/benchmarks/run.py `
  --moulang build/release-0.3.0/compiler/bin/moulang.exe `
  --cxx .toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe `
  --samples 5 --output build/benchmarks/rerun.csv
```

The runner verifies every result. It writes median compile time, elapsed execution time, peak working set, and executable size to the specified CSV and a Markdown report beside it. The first run of each workload is a warm-up. Execution includes process startup and one captured result. Windows reports the peak for each process. Test input and output files are kept in a fresh directory under build/benchmarks.

The built-in sort rows measure different library implementations. The ASCII and Hangul sequential scans use the same number of code points and passes in C++ and CPython. Moulang currently provides only the separate indexed sweep for this string workload. The native compile time and executable size columns are N/A for CPython.

Keep the same machine, inputs, sample count, compiler flags, and process startup conditions when comparing with a future ARC build. Use a different output name for the new results. Pass `--moulang-opt=-O0` or `--moulang-opt=-O3` to run the Moulang variants.

For a compiler with string foreach support, pass `--string-foreach` to measure the ASCII and Hangul sequential sweeps only. Each case verifies 1,200,000 code-point comparisons across 300 passes. Use a separate CSV name to preserve the historical baseline.

Pass `--string-index` to measure repeated positive and negative indexing of 16- and 16,384-code-point ASCII and Hangul strings. Run it once with the pre-optimization compiler and once with the updated compiler, each paired with its own runtime archive. Use the same sample count and a different output name for each run. The runner verifies every timed case.

Pass `--list-traversal list_indexed` to measure the equivalent manual loop on both builds. Pass `--list-traversal list_foreach` on the updated build to measure the guarded loop. Both cases check all 4,096 elements in each of 1,000 passes.

Pass `--int-lists` to compare appending and traversing one million elements in a legacy slice and a shared integer list. The two write cases measure 128 writes after constructing the same 262,144-element list. One case pins a new snapshot view before each write and verifies its old value.

Pass `--integer-casts` to compare explicit byte and short casts before and after range checks. Each case runs 12 million iterations. One obtains its input from a list runtime call. The other derives bounded values with a bit mask. Use the same flags, sample count, and fixture with both compiler builds and record their outputs separately.

For a Tiki check benchmark, run `python -B tests/benchmarks/tiki.py --before <previous-compiler> --after <tiki-compiler> --output <report.csv>`. It verifies quiet successful checks on two generated source graphs and records the elapsed time and peak working set for off, Basic, Performance, and All. Each measurement includes process startup and uses a warm-up followed by seven timed runs by default.

Add `--deep` with a Stage 14 compiler as `--before` and a Deep-capable compiler as `--after` to compare plain, Basic, and Deep checks on source graphs with list aliases. A separate workload emits 50 Deep warnings and checks their codes before measuring output cost.

The [Stage 16 baseline](../../docs/progress/PERFORMANCE_OPTIMIZATION_DESIGN.md#reproduce-the-baseline) also uses `utf8-index-direction.m` and `text-concat-join.m`. Compile each at `-O2`. Each executable prints in-process timings in nanoseconds in source order and prints `-1` for any incorrect result. Discard the warm-up launch and record 11 samples for comparison with the saved measurements.
