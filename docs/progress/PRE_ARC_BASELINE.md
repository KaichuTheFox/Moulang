# Pre-ARC benchmark results

- Hardware is AMD Ryzen 5 5600X 6-Core Processor.
- OS is Windows 10.0.26300 (AMD64).
- Moulang uses Moulang 0.3.0 with -O2.
- C++20 uses clang version 22.1.8 with -std=c++20 -O2 -static.
- CPython uses 3.14.6 with -B.

Reported values are medians of 5 measured runs after one warm-up.
Execution includes process startup, computation and a single captured result. Peak working set is the process's Windows-reported resident memory high-water mark.
Native compile times include compiler launch and linking. CPython runs source with -B, so native compile time and EXE size are N/A.
List sort rows time each implementation's built-in sorting. The indexed sweep is a separate Moulang workload. Moulang string foreach is N/A.
File I/O uses fresh directories on the same host. File caches may affect the results.

| Workload | Language | Compile ms | Execute ms | Peak bytes | EXE bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| integer_math | Moulang | 113.354 | 57.647 | 4857856 | 210432 |
| integer_math | C++20 | 1964.446 | 106.507 | 5099520 | 1472512 |
| integer_math | CPython | N/A | 113.556 | 14139392 | N/A |
| real_math | Moulang | 111.048 | 62.943 | 4886528 | 560128 |
| real_math | C++20 | 1966.338 | 105.326 | 5136384 | 1472512 |
| real_math | CPython | N/A | 122.948 | 14213120 | N/A |
| loop_count | Moulang | 110.673 | 56.834 | 4857856 | 210432 |
| loop_count | C++20 | 1997.913 | 100.768 | 5103616 | 1473024 |
| loop_count | CPython | N/A | 148.241 | 14118912 | N/A |
| function_calls | Moulang | 113.073 | 61.679 | 4866048 | 210432 |
| function_calls | C++20 | 1971.485 | 105.081 | 5107712 | 1473024 |
| function_calls | CPython | N/A | 121.229 | 14127104 | N/A |
| string_concat | Moulang | 111.683 | 54.714 | 11141120 | 89088 |
| string_concat | C++20 | 1967.065 | 104.548 | 5099520 | 1472000 |
| string_concat | CPython | N/A | 91.957 | 14295040 | N/A |
| list_access | Moulang | 110.984 | 57.523 | 4870144 | 214528 |
| list_access | C++20 | 1935.939 | 101.188 | 5115904 | 1472000 |
| list_access | CPython | N/A | 96.462 | 14147584 | N/A |
| list_sort | Moulang | 119.896 | 58.831 | 4878336 | 219648 |
| list_sort | C++20 | 1993.278 | 103.715 | 5111808 | 1472000 |
| list_sort | CPython | N/A | 102.946 | 14143488 | N/A |
| ascii_foreach | Moulang | N/A | N/A | N/A | N/A |
| ascii_foreach | C++20 | 1977.847 | 103.825 | 5103616 | 1472000 |
| ascii_foreach | CPython | N/A | 128.814 | 14360576 | N/A |
| hangul_foreach | Moulang | N/A | N/A | N/A | N/A |
| hangul_foreach | C++20 | 1953.028 | 108.259 | 5111808 | 1472000 |
| hangul_foreach | CPython | N/A | 146.833 | 14184448 | N/A |
| ascii_indexed | Moulang | 111.621 | 62.011 | 4870144 | 212480 |
| ascii_indexed | C++20 | N/A | N/A | N/A | N/A |
| ascii_indexed | CPython | N/A | N/A | N/A | N/A |
| hangul_indexed | Moulang | 112.304 | 68.836 | 4882432 | 212480 |
| hangul_indexed | C++20 | N/A | N/A | N/A | N/A |
| hangul_indexed | CPython | N/A | N/A | N/A | N/A |
| file_io | Moulang | 109.272 | 76.744 | 5468160 | 91648 |
| file_io | C++20 | 2060.029 | 135.262 | 5410816 | 1472000 |
| file_io | CPython | N/A | 118.251 | 14909440 | N/A |
