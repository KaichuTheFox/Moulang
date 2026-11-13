# Moulang benchmark results

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
| integer_math | Moulang | 114.131 | 56.461 | 4853760 | 210432 |
| integer_math | C++20 | 2079.425 | 111.460 | 5095424 | 1472512 |
| integer_math | CPython | N/A | 119.851 | 14123008 | N/A |
| real_math | Moulang | 119.762 | 66.582 | 4878336 | 560128 |
| real_math | C++20 | 2071.989 | 105.227 | 5132288 | 1472512 |
| real_math | CPython | N/A | 126.387 | 14151680 | N/A |
| loop_count | Moulang | 129.856 | 69.803 | 4853760 | 210432 |
| loop_count | C++20 | 2144.025 | 107.226 | 5095424 | 1473024 |
| loop_count | CPython | N/A | 175.130 | 14139392 | N/A |
| function_calls | Moulang | 137.250 | 66.807 | 4857856 | 210432 |
| function_calls | C++20 | 2248.480 | 120.819 | 5107712 | 1473024 |
| function_calls | CPython | N/A | 128.229 | 14127104 | N/A |
| string_concat | Moulang | 114.385 | 52.458 | 4845568 | 89600 |
| string_concat | C++20 | 2019.893 | 103.417 | 5091328 | 1472000 |
| string_concat | CPython | N/A | 101.089 | 14364672 | N/A |
| list_access | Moulang | 133.470 | 66.732 | 4866048 | 215040 |
| list_access | C++20 | 2140.755 | 163.247 | 5111808 | 1472000 |
| list_access | CPython | N/A | 118.696 | 14147584 | N/A |
| list_sort | Moulang | 127.872 | 71.486 | 4870144 | 222208 |
| list_sort | C++20 | 2161.294 | 148.591 | 5107712 | 1472000 |
| list_sort | CPython | N/A | 104.895 | 14163968 | N/A |
| ascii_foreach | Moulang | N/A | N/A | N/A | N/A |
| ascii_foreach | C++20 | 2071.395 | 114.050 | 5103616 | 1472000 |
| ascii_foreach | CPython | N/A | 130.849 | 14192640 | N/A |
| hangul_foreach | Moulang | N/A | N/A | N/A | N/A |
| hangul_foreach | C++20 | 1983.420 | 106.650 | 5107712 | 1472000 |
| hangul_foreach | CPython | N/A | 147.793 | 14180352 | N/A |
| ascii_indexed | Moulang | 113.711 | 62.767 | 4866048 | 212992 |
| ascii_indexed | C++20 | N/A | N/A | N/A | N/A |
| ascii_indexed | CPython | N/A | N/A | N/A | N/A |
| hangul_indexed | Moulang | 125.656 | 73.643 | 4861952 | 212992 |
| hangul_indexed | C++20 | N/A | N/A | N/A | N/A |
| hangul_indexed | CPython | N/A | N/A | N/A | N/A |
| file_io | Moulang | 111.428 | 77.598 | 5206016 | 93184 |
| file_io | C++20 | 2073.138 | 133.744 | 5406720 | 1472000 |
| file_io | CPython | N/A | 121.051 | 14712832 | N/A |
