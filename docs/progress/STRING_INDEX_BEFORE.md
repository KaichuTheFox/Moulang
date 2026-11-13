# Moulang benchmark results

- Hardware is AMD Ryzen 5 5600X 6-Core Processor.
- OS is Windows 10.0.26300 (AMD64).
- Moulang uses Moulang 0.3.0 with -O2.

Reported values are medians of 5 measured runs after one warm-up.
Execution includes process startup, computation and a single captured result. Peak working set is the process's Windows-reported resident memory high-water mark.
Native compile times include compiler launch and linking. CPython runs source with -B, so native compile time and EXE size are N/A.
Each iteration checks a positive and a negative index. Short strings have 16 code points and long strings have 16384.

| Workload | Language | Compile ms | Execute ms | Peak bytes | EXE bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| ascii_index_short | Moulang | 117.743 | 58.515 | 4870144 | 212992 |
| ascii_index_long | Moulang | 122.198 | 152.513 | 4874240 | 212992 |
| hangul_index_short | Moulang | 117.900 | 61.389 | 4866048 | 212992 |
| hangul_index_long | Moulang | 116.731 | 281.553 | 4907008 | 212992 |
