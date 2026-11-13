# Moulang benchmark results

- Hardware is AMD Ryzen 5 5600X 6-Core Processor.
- OS is Windows 10.0.26300 (AMD64).
- Moulang uses Moulang 0.3.0 with -O2.

Reported values are medians of 5 measured runs after one warm-up.
Execution includes process startup, computation and a single captured result. Peak working set is the process's Windows-reported resident memory high-water mark.
Native compile times include compiler launch and linking. CPython runs source with -B, so native compile time and EXE size are N/A.
Each list traversal verifies 4096000 comparisons across 1000 passes.

| Workload | Language | Compile ms | Execute ms | Peak bytes | EXE bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| list_foreach | Moulang | 122.404 | 89.959 | 4874240 | 216064 |
