# Moulang benchmark results

- Hardware is AMD Ryzen 5 5600X 6-Core Processor.
- OS is Windows 10.0.26300 (AMD64).
- Moulang uses Moulang 0.3.0 with -O2.

Reported values are medians of 5 measured runs after one warm-up.
Execution includes process startup, computation and a single captured result. Peak working set is the process's Windows-reported resident memory high-water mark.
Native compile times include compiler launch and linking. CPython runs source with -B, so native compile time and EXE size are N/A.
The append/read cases append and read 1,000,000 values. The write cases fill a list of 262,144 elements and perform 128 writes, with or without a new pinned view per write.

| Workload | Language | Compile ms | Execute ms | Peak bytes | EXE bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| int_slice_append_read | Moulang | 135.937 | 73.887 | 13230080 | 211968 |
| int_list_append_read | Moulang | 119.247 | 67.191 | 11542528 | 214016 |
| int_list_write | Moulang | 118.742 | 61.824 | 6975488 | 213504 |
| int_list_view_write | Moulang | 132.380 | 93.276 | 8556544 | 215040 |
