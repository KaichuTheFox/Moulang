# Moulang benchmark results

- Hardware is AMD Ryzen 5 5600X 6-Core Processor.
- OS is Windows 10.0.26300 (AMD64).
- Moulang uses Moulang 0.3.0 with -O2.

Reported values are medians of 7 measured runs after one warm-up.
Execution includes process startup, computation and a single captured result. Peak working set is the process's Windows-reported resident memory high-water mark.
Native compile times include compiler launch and linking. CPython runs source with -B, so native compile time and EXE size are N/A.
Each case performs 12,000,000 iterations with two integer narrowing casts. The runtime case reads values through the list runtime. The bounded case derives values from a bit mask in the loop.

| Workload | Language | Compile ms | Execute ms | Peak bytes | EXE bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| runtime_integer_casts | Moulang | 120.653 | 162.100 | 4968448 | 214016 |
| bounded_integer_casts | Moulang | 117.366 | 58.215 | 4960256 | 210944 |
