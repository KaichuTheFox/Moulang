# Stage 6 ARC validation

The [implementation status](../IMPLEMENTATION_STATUS.md) records the supported lifetime rules. The [ownership design](ARC_DESIGN.md) describes the C ABI, Debug handles, and remaining class and integer-slice lifetime boundaries.

## Before and after

Each entry is a median of five runs after a warm-up on the same Windows x64 host. These Moulang executables were compiled at -O2. Each time includes process startup and captured output. Peak working set measures the whole process. See the [before CSV](PRE_ARC_BASELINE.csv), [after CSV](POST_ARC_BASELINE.csv), [after benchmark report](POST_ARC_BASELINE.md), and [reproduction instructions](../../tests/benchmarks/README.md).

| Workload | Before ms | After ms | Before peak bytes | After peak bytes |
| --- | ---: | ---: | ---: | ---: |
| Integer math | 57.647 | 56.461 | 4857856 | 4853760 |
| Real math | 62.943 | 66.582 | 4886528 | 4878336 |
| Loop count | 56.834 | 69.803 | 4857856 | 4853760 |
| Function calls | 61.679 | 66.807 | 4866048 | 4857856 |
| String concatenation | 54.714 | 52.458 | 11141120 | 4845568 |
| List access and mutations | 57.523 | 66.732 | 4870144 | 4866048 |
| Built-in list sort | 58.831 | 71.486 | 4878336 | 4870144 |
| ASCII indexed sweep | 62.011 | 62.767 | 4870144 | 4866048 |
| Hangul indexed sweep | 68.836 | 73.643 | 4882432 | 4861952 |
| File I/O | 76.744 | 77.598 | 5468160 | 5206016 |

Repeated concatenation's peak fell by about 56.5 percent in this workload. List access and sort elapsed times increased in this run. The integer-loop and C++ comparison measurements also shifted between runs, so these timing differences include run-to-run variation. The CSVs include compile time and executable size for each workload. The CTest stress fixture verified one million bounded concatenations under the 256 MiB live budget and Debug reported zero remaining managed objects.

## Test coverage

- Generated executable tests cover escaping slices and child lists, shared mutation and reassignment, struct fields, nullable values, class roots, file-read slices, scope exits, and -O0, -O2, and -O3.
- All 11 top-level examples produced identical output with the baseline and ARC compilers.
- The Debug test rejects stale string and list handles before their managed storage is read and reports a deliberately retained object. The Release test runs with the same reporting environment and produces empty standard error. Runtime symbol inspection finds the diagnostic entry points in Debug only.
- A native Windows AddressSanitizer run compiled the runtime and a C++ harness that repeated 100000 shared string and list allocation and release cycles. It completed without a sanitizer report. The sanitizer run covered native runtime code. Generated Moulang IR ran through the normal compiler tests.
- The hosted workflow and clean Windows virtual machines remain separate verification environments.
