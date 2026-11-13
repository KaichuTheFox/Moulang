# Stage 16 performance optimization

The owner approved A. The runtime implements directional UTF-8 indexing.

## Baseline and method

The original [Stage 5 baseline](PRE_ARC_BASELINE.md) and [Stage 6 ARC validation](ARC_VALIDATION.md) measured Windows x64 execution, native compile time, peak working set, and executable size. In the historical run, bounded concatenation peak working set fell from 11,141,120 to 4,845,568 bytes after ARC. Timings for the list access and sorting workloads increased in that run. The current tree also includes the later ASCII indexing, UTF-8 `foreach`, integer-list, and Tiki changes. The new side-by-side comparison therefore measures the combined changes since the pre-ARC build.

The baseline runner compiles each workload at `-O2`, verifies its output on every run, and reports a median after warm-up. Its elapsed time includes process startup. The directional indexing probe times the indexing loop inside a running Moulang process using `std:time.monotonicNanos()` and checks every comparison. It times string lengths and index directions separately.

## Measurements before optimization

The [native baseline CSV](STAGE16_BASELINE.csv) records the saved pre-ARC compiler and the Stage 15 Release compiler on the same Ryzen 5 5600X Windows x64 host. Both used `-O2`. The Stage 5 workloads were run in two batches of five measured samples after warm-up. Bounded concatenation peak working set fell from 10,510,336 to 4,259,840 bytes. The file I/O median rose from 52.271 to 60.248 ms and the function-call case rose from 32.277 to 36.399 ms. These runs compare different compiler and runtime builds. Launch overhead, file caching, and timing variation affect short cases. The two increases should be checked again in the post-optimization run.

In seven measured samples of the targeted index suite, the 16,384-code-point Hangul case took 247.378 ms, compared with 32.078 ms for the same-length ASCII case. Both cases check a positive and a negative index in each of 8,000 iterations. Current `foreach` sweeps took 40.615 ms for ASCII and 42.149 ms for Hangul over 1,200,000 comparisons each. The foreach and index suites do different work.

The [in-process probes](STAGE16_LOOP_PROBES.csv) record 11 samples after warm-up. In 8,000 comparisons on the long Hangul string, `[0]` took 32.736 ms, `[-1]` took 203.787 ms, and `[16383]` took 198.822 ms. The 16-code-point `[0]` case took 0.111 ms. The size effect on `[0]` is consistent with the full count pass. The extra time at either spelling of the last position is consistent with the second traversal.

Collecting 16,000 pieces and joining once took 0.294 ms in the separate text probe. Repeated concatenation took 2.822 ms and produced the same final text. The integer-list suite took 33.855 ms for the plain write case and 56.338 ms with a pinned snapshot before each write. That case verifies the snapshot's old values. It includes the cost of creating views as well as detaching buffers.

## A. Traverse UTF-8 from the end selected by the index

Before Stage 16, `moulang_text_index` counted all code points in a non-ASCII string, then started at the first byte to locate the normalized index. The new path walks forward for a nonnegative index and backward for a negative index. `text[0]` and `text[-1]` each visit at most one code point. A lookup at a valid nonnegative index `i` takes O(i + 1) time. A valid negative index takes O(|i|). Both have O(n) worst-case time for n code points and use O(1) extra space. ASCII indexing uses direct byte offsets in O(1) time.

Both paths use the slice's visible byte range and retain the owner of the returned code point. The byte length bounds possible indices, so empty input and extreme signed indices are rejected without taking an absolute value or walking past either end. The runtime retains its existing diagnostic and adds no metadata, allocations, or compiler options.

## B. Add sparse offset metadata for long UTF-8 strings

Deferred until an application shows a measurable random-indexing bottleneck after the directional scan. Build a small table of code-point checkpoints on demand for qualifying immutable strings. Repeated access far from either end could then scan from the nearest checkpoint. The initial scan and metadata allocation add cost to the first indexed use. Cached storage also adds pressure to the ARC memory budget. A future design would define a threshold and memory cap, account for sharing and reclamation, and validate the byte origin for string slices. Inputs outside the cache policy would use ordinary traversal.

## Other candidates

- String construction and temporary objects. The existing `std:text` join builds the result in one allocation when only the final concatenated string is needed. An automatic accumulator rewrite would need to preserve intermediate reads, effects, and allocation errors. Measure an equivalent join workload before proposing such a rewrite.
- Collection allocations. An integer-list snapshot can keep the old buffer alive, requiring a copy before a write. Any optimization must preserve snapshot isolation and live-budget accounting. In-place mutation is available when the buffer is unshared. The baseline also distinguishes string-list access from sorting.
- ARC retain and release. Calls around list items and text slices are candidates for profiling. Removing a call requires evidence that its owner remains live across calls, mutations, and scope exits. The existing runtime checks and ownership tests define the verification boundary.
- Loop code generation, function calls, and LLVM options. The current compiler already uses LLVM's default optimization pipelines for `-O2` and `-O3`. Timing for a whole short program includes startup. A change to the default pipeline requires a repeatable workload gain and a check of compile time, executable size, and every supported optimization mode.

## C. Keep the implementation and measure application workloads

Use `foreach` for sequential code-point reads and apply the existing join hint where the result is only needed at the end. Measure real workloads that require random indexing before changing the runtime. This keeps the current memory and compatibility profile. Repeated indexing of long non-ASCII text retains its current scan cost.

## Reproduce the baseline

Use the [benchmark runner](../../tests/benchmarks/README.md) with separate output paths for the saved pre-ARC compiler and a Stage 15 Release build. Run both with five samples and `-O2`. With the Stage 15 compiler, run `--string-index`, `--string-foreach`, and `--int-lists` with seven samples each. Keep each compiler paired with its own runtime archive. Compile `utf8-index-direction.m` and `text-concat-join.m` from the benchmark directory at `-O2`, then launch each executable 12 times. Discard the first launch. The probes print one elapsed value in nanoseconds per case, in source order. A negative value indicates failed verification.

## Verification after approval

- Windows x64 Release and Debug built successfully. Release CTest passed 27 of 27 and Debug CTest passed 24 of 24, including ARC, ARC with Tiki, and the existing print-block cases. The isolated VS Code 1.144.0 source extension and installed VSIX suites passed. Document and editor unit checks passed 10 of 10.
- The expanded indexing fixture passed on both the Stage 15 reference runtime and the updated Release runtime. The Debug test checked zero remaining managed objects after exercising indexing on slices retained across rebinding, emoji, Hangul, combining marks, and NUL. The bounds cases verified exit code 1 and the original diagnostic text at the exact subscript column, including out-of-range indices within the UTF-8 byte count and both signed integer extremes.
- Hosted CI, a clean Windows VM, and the minimum VS Code version were outside this local run. The [isolated editor instructions](../../tests/vscode/README.md) and [Windows build instructions](../../README.md#build-from-source) describe how to reproduce the local tests.

## Measurements after optimization

The same Windows x64 host, fixture sources, Release configuration, `-O2`, warm-up, and sample counts were used. The [native results](STAGE16_OPTIMIZED.csv) record compile time, execution time, peak working set, and executable size. The [loop results](STAGE16_OPTIMIZED_LOOP_PROBES.csv) record median and range without process startup. The same workload outputs passed verification on both builds.

- The 16,384-code-point Hangul workload with positive and negative indices dropped from 247.378 to 106.455 ms, a 57.0 percent reduction. The executable stayed at 214,016 bytes. Median peak working set was 4,308,992 before and 4,313,088 bytes after.
- For 8,000 individual reads on the long Hangul string, `[0]` dropped from 32.736 to 0.088 ms and `[-1]` dropped from 203.787 to 0.093 ms. `[16383]` dropped from 198.822 to 150.882 ms. The positive index near the far end still traverses the string. The 16-code-point `[0]` case dropped from 0.111 to 0.088 ms.
- The separate-batch ASCII medians rose from 32.536 to 35.117 ms for short strings and from 32.078 to 33.016 ms for long strings. The in-process ASCII `[0]` probe rose from 69.1 to 72.5 microseconds for 8,000 reads. ASCII still uses direct byte offsets. Real math rose from 37.867 to 46.164 ms and list sorting rose from 34.747 to 35.299 ms. File I/O fell from 60.248 to 56.265 ms. Collection and foreach medians shifted in both directions, as recorded in the CSV.
- In a separate cross-check, the same cases were compiled once per build and executed in alternating order in fresh directories, with 15 measured samples after warm-up. Long Hangul fell from 231.781 to 83.154 ms, with disjoint observed ranges. Short ASCII rose from 8.959 to 9.133 ms, short Hangul rose from 10.987 to 11.217 ms, and real math rose from 12.631 to 12.880 ms, with overlapping ranges. Long ASCII fell from 8.406 to 8.309 ms and file I/O from 51.086 to 45.836 ms, also with overlapping ranges. Alternating the in-process probe for 11 samples showed an ASCII `[0]` median of 69.2 before and 72.6 microseconds after for 8,000 reads. The timing increase in this small probe was about 0.43 ns per read.

The production changes are confined to `runtime/src/runtime.cpp`. Regression coverage changed in `tests/integration/ascii-index.ps1`. The language specification, user guide, implementation status, changelog, and this report describe the behavior and measurements.
