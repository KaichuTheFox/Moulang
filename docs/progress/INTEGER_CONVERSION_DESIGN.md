# Stage 10 integer conversion design

Status is approved and implemented under policy A.

## Rules before Stage 10

The [Stage 1 conversion matrix](UPDATE_AUDIT.md#numeric-conversion-matrix) records the full numeric behavior. The integer destination ranges are 0 through 255 for unsigned `char`, -32768 through 32767 for signed `short`, and -2147483648 through 2147483647 for signed `int`.

- `short`, `int`, and `long` to `char` keep the low 8 bits.
- `int` and `long` to `short` keep the low 16 bits and interpret them as signed.
- `long` to `int` checks the signed 32-bit range, as approved in [R2](../LONG_DESIGN.md#r-explicit-long-to-int-conversion).

The Stage 9 Release compiler produced 255 for `char(-1)`, 4 for `char(260)`, and -1 for `short(65535)`. Both `int(2147483648)` and `int(-2147483649)` exited with code 1 and reported the cast line. An implicit assignment from `int` to `short` reported ML3007. The probes also confirmed integer wrap in `short(-32768) / short(-1)` and `2147483647 + 1`, and the existing real conversions `int(2.5) == 2`, `int(3.5) == 4`, and `int(4294967297.0) == 1`.

The Release integration, long, and relocation CTest entries passed. The explicit masks in A produced 4 and -1 in a separate migration probe.

## A. Check all explicit integer narrowing

Require the input integer to fit the destination before converting. This applies to all six source and destination pairs listed above. An out-of-range value produces a runtime error at the cast site, including a constant that is executed. The program reports the source file, line, column, and destination range error. Lossless implicit widening, overload ranking, arithmetic wrap, and the current real conversion rules retain their existing behavior.

- Compatibility. The approved `long` to `int` check continues to work. Existing casts such as `char(260)` and `short(65535)` change from successful wrap to an error. The [numbers example](../../examples/numbers.m) uses `char(260 & 255)` to keep its previous output. For deliberate signed 16-bit wrap use `short((value & 32767) - (value & 32768))` with an `int` or `long` input.
- Safety and usability. The same cast rule applies to every integer destination. Values such as negative byte counts and oversized indices fail at their conversion site. Intentional bit truncation is visible in source.
- Performance. A checked cast has a range check on its success path. LLVM can fold comparisons whose inputs or ranges are known when those checks are emitted in IR. Measure casts inside loops with in-range values whose ranges are unknown at compile time. Record constant folding separately.

## B. Preserve low bits and offer checked casts

Make all six explicit integer narrowing pairs retain the destination's low bits. Provide explicit checked operations for conversion to `char`, `short`, and `int`. Function spellings such as `checkedChar`, `checkedShort`, and `checkedInt` are candidates, and their name resolution and potential collisions with user functions would need review.

- Compatibility. The existing byte and short wrapping programs keep their results. The approved R2 behavior changes. `int(2147483648)` would return -2147483648 and `int(-2147483649)` would return 2147483647. Programs relying on the current failure would need to use the checked operation.
- Safety and usability. Direct casts are convenient for binary data and modular arithmetic. Ordinary application code must choose the checked form to catch an out-of-range integer.
- Performance. Direct narrowing can lower to an LLVM truncation. Only checked operations pay the range-check cost.

## Decision and implementation

The owner approved A. It preserves the `long` to `int` check and applies the same range rule across integer destinations. See the [migration guide](../USER_GUIDE.md#migrate-integer-narrowing-casts) for explicit low-bit operations. Real-to-integer rounding and low-bit behavior retain their existing rules.

The compiler evaluates an explicit cast operand once. For casts to `char` and `short`, it checks whether extending the narrowed value back to the source type reproduces the original. The extension is unsigned for `char` and signed for `short`. The failure branch sets the cast location and reports a range error. Casts from `long` to `int` call the existing checked runtime helper. Internal arithmetic conversions use the existing wrap behavior, so `short(-32768) / short(-1)` still yields -32768.

## Validation

Release passed 23 CTest entries and Debug passed 20. The new test covered all six integer narrowing pairs, destination boundaries, both `long` extremes, a failing literal cast, and exact error locations at `-O0`, `-O2`, and `-O3`. It also checked single evaluation of the operand and the established arithmetic, real conversion, and print block behavior. The relocation test ran the migrated example. VS Code unit tests and isolated source and installed VSIX suites passed, including the task exit codes for an out-of-range cast and its masked equivalent.

## Performance

Each executable was built at `-O2` on the same Windows x64 machine. The workload checks the sum after 12 million iterations, with a byte cast and a short cast per iteration. The reported median uses seven runs after one warm-up and includes process startup, workload setup, and output.

- With independently loaded list values, execution took 149.943 ms before and 162.100 ms after, an increase of 8.1% in this workload. The timings include the two list reads per iteration.
- With values bounded by bit masks in the loop, execution took 57.704 ms before and 58.215 ms after, a difference of about 0.5 ms. These inputs give the optimizer an opportunity to eliminate checks. These measurements cover the whole program.

See the [before](INTEGER_CAST_BEFORE.csv) and [after](INTEGER_CAST_AFTER.csv) results for compile time, peak working set, and executable size.

## Changed files

- Checked cast emission in `src/codegen/llvm/object.cpp`.
- Low-bit example and regression updates in `examples/numbers.m` and `tests/integration/compiler.ps1`.
- CTest registration, checked cast fixtures, and VS Code regressions in `CMakeLists.txt`, `tests/integration/integer-casts.ps1`, `tests/integration/run-source.ps1`, and `tests/vscode/suite.js`.
- Benchmark case, runner, instructions, and results in `tests/benchmarks/integer-casts.m`, `tests/benchmarks/run.py`, `tests/benchmarks/README.md`, and the before and after reports.
- Language rules, migration, status, and design references in `CHANGELOG.md`, `docs/LANGUAGE_SPEC.md`, `docs/USER_GUIDE.md`, `docs/IMPLEMENTATION_STATUS.md`, `docs/LONG_DESIGN.md`, and `docs/README.md`.
