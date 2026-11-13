# Moulang benchmark results

- Hardware is AMD Ryzen 5 5600X 6-Core Processor.
- OS is Windows 10.0.26300 (AMD64).
- Moulang uses Moulang 0.3.0 with -O2.
- C++20 uses clang version 22.1.8 with -std=c++20 -O2 -static.
- CPython uses 3.14.6 with -B.

Reported values are medians of 5 measured runs after one warm-up.
Execution includes process startup, computation and a single captured result. Peak working set is the process's Windows-reported resident memory high-water mark.
Native compile times include compiler launch and linking. CPython runs source with -B, so native compile time and EXE size are N/A.
Each sweep compares 4000 code points across 300 passes and verifies 1200000 matches.

| Workload | Language | Compile ms | Execute ms | Peak bytes | EXE bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| ascii_foreach | Moulang | 119.192 | 68.127 | 4853760 | 211968 |
| ascii_foreach | C++20 | 2040.828 | 106.823 | 5095424 | 1472000 |
| ascii_foreach | CPython | N/A | 132.906 | 14163968 | N/A |
| hangul_foreach | Moulang | 116.316 | 68.791 | 4849664 | 211968 |
| hangul_foreach | C++20 | 2038.545 | 110.267 | 5095424 | 1472000 |
| hangul_foreach | CPython | N/A | 159.525 | 14163968 | N/A |
