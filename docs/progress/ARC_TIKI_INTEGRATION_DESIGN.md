# Stage 15 ARC and Tiki Inspector integration

The owner approved A. Deep is implemented as an opt-in group with one rule.

## Stage 14 baseline

- ARC manages string buffers, string lists, integer lists, and their owned views. A string slice can retain its backing buffer after the source binding changes. An integer-list view retains a snapshot when a write detaches the list's buffer. Fixed-array views borrow storage and are checked during semantic analysis. Classes and legacy heap integer slices have process lifetime.
- The Debug runtime validates managed handles before instrumented reads. The existing native fixture exercises released string and list handles and deliberately retains a live object. A successful generated program can report live object and byte counts at exit. Class roots have separate counters. Release omits the Debug instrumentation.
- Managed string-list edges descend from `str[][]` to `str[]` to `str`. A preliminary CLI check accepted two slots sharing one row with Tiki All enabled. Existing type checks rejected self-insertion into `str[][]` (`ML3007`) and appending a parent to its `str[]` child (`ML3011`). This covers those invalid list edges. Class references and source import cycles use different rules.
- `TI-P001` identifies repeated string accumulation that can keep allocating and copying. ARC reclaims an intermediate once its last owner releases it. A clean exit report and a copying warning can both be correct for the same program.
- Stage 14 supported Basic, Performance, and All and rejected `--tiki=deep`. The [Stage 14 benchmark](TIKI_BENCHMARK.csv) records those modes on the earlier workload.

## A. Validate existing behavior and add one opt-in Deep rule

Recommended. Start with the shared regression plan below. Add one Deep warning for an `append` reached in a `foreach` over the same list. Track a local list and direct alias assignments through straight-line statements in the function. Require a definite match with an active iterator at the operation. Drop facts when a binding changes in a way the analysis cannot resolve. Leave branches, calls, fields, and nested container aliasing outside the first rule. Distinguish appending to the active list from replacing an element, rebinding a local, or appending to an independent list. The hint can suggest postponing the append when traversing the original list is intended and the change preserves evaluation order.

Expose the rule through `check --tiki=deep`, include it in opt-in `all`, and support the existing per-rule disable option. If the rule ships, give it a stable `TI-D` diagnostic code and offer `deep` in the existing opt-in VS Code setting. Ordinary `check` keeps its current behavior. The runtime check continues to reject structural changes during active iteration, including alias patterns beyond the static rule. The Deep warning gives an earlier source location for supported patterns. Managed cycle analysis remains tied to a future type that can form a strong ownership cycle.

Measure the added compiler work before extending the rule set. The initial rule deliberately leaves possible alias paths undiagnosed when the analysis cannot prove them. Publishing `deep` and adding it to `all` expands the output of existing opt-in `all` checks.

## B. Validate existing behavior and defer Deep

Run the shared regression plan using the current CLI and VS Code modes. This covers lifetime checks, list topology, and the current warning hints without adding a diagnostic or changing `all`. Record the Deep benchmark as pending until a separate design is approved. This leaves the Basic versus Deep comparison open and does not add an early warning for known list mutation through an alias.

## Shared regression plan

1. Check and execute the same valid programs. Verify exact output and normal exit. In Debug, check for zero remaining managed objects and bytes on fixtures without class roots. Cover a string slice that survives its original binding, a surviving shared child row, integer-list snapshot detachment, and a safe traversal with element replacement. Include a separate class-root fixture and assert its root counters separately.
2. Reuse the native stale-handle cases for managed strings and lists. Assert the runtime failure and the absence of a normal-exit report. Check fixed-array borrow errors and invalid managed-list edges through the compiler. Assert that valid shared rows yield no ownership or cycle warning. Attribute type rejection, Debug handle checks, and normal output to their respective assertions.
3. Check a valid string accumulator for `TI-P001` and run it under the Debug runtime. Test the suggested collection and `std:text` `join` rewrite on the same fixed input, including empty pieces, multi-byte code points, and a NUL byte. Compare captured output bytes and the relevant warning counts. Limit the equivalence claim to programs where only the final string is read and evaluation order is preserved.
4. Run Release and Debug CTest and isolated VS Code source and installed VSIX regressions. Check warning severity, file and range, disabled modes, error precedence, and stale diagnostic removal. Keep the existing `print()` and block `format()` fixtures in the regression run. Record uncovered cases explicitly.

## Additional checks for A

- Label positive and negative Deep fixtures before implementing the rule. Include a direct list reference, an unchanged alias, a rebound alias, an independent list, and indexed replacement. Check that any warned mutation actually hits the existing runtime guard when reached. Use separate clean-exit fixtures for allowed operations.
- Measure `check`, `--tiki=basic`, and `--tiki=deep` on the same Release compiler and source graphs. Use one warm-up per mode and at least seven timed samples with rotating order. Reuse a small and a larger multi-file workload, and include alias patterns that exercise the rule. Time warning-free cases separately from warning emission. Report median, observed range, and peak working set. Record startup as part of the measured time.

## Deep rule

`TI-D001` identifies `append`, `insert`, `remove`, and `pop` on a list known to be the same object captured by an active `foreach`. It covers `int[]`, `str[]`, and `str[][]`. It points at the method name and explains that the runtime rejects structural changes during iteration. The hint suggests collecting changes and applying them after the loop when the intent is to visit the original items. Each operation still validates its arguments at runtime. `sort` is outside this rule because an already ordered list can be sorted during traversal without changing its structure.

Within a function, copying a list binding copies its tracked identity. This includes direct copies of a parameter or loop-local row. Rebinding the iterator source variable leaves the captured list active. The analysis retains facts that agree across branches and forgets identities that can change across iterations. It skips some provably unreachable operations, such as a literal false branch or a statement after an unconditional `continue`. Indexed replacement and iteration over a read-only snapshot produce no `TI-D001`.

Results returned by calls and lists loaded through fields or nested slots have no inferred relationship to other bindings. A direct copy of such a result still shares the binding's tracked identity. The analysis does not track aliases between parameters, look through calls, prove that a dynamic branch will run, or infer whether `remove` will find a value. A quiet check leaves uncertain cases to the existing runtime guard. Current managed list types cannot form a strong cycle through list slots. The invalid edge tests exercise existing type errors. A cycle diagnostic would require a managed type that permits a cycle.

Run `moulang check main.mcx --tiki=deep` to select this group, or use `--tiki=all --tiki-disable=TI-D001` to run the other groups. The trusted-workspace Live Diagnostics setting `moulang.tikiMode` accepts `deep`. Ordinary `check` still runs language and safety checks by default.

## Verification results

- Windows x64 Release and Debug builds passed. Release CTest passed 27 of 27 and Debug CTest passed 24 of 24. Both ran `moulang.arc-tiki` and the existing `moulang.arc` suite. The new Debug integration test also passed under Windows PowerShell 5.1. The integration test matched six expected Deep findings to actual runtime rejections. Valid fixtures covered indexed writes, sorted no-op sorts, alias rebinding across iterations, a shared row, a retained string slice, and an integer snapshot. The Debug runs reported zero remaining managed objects and bytes for these normal exits. The separate ARC suite verified class-root accounting and native stale string and list handle rejection.
- The concatenation program emitted `TI-P001` and the `join` rewrite checked quietly. Both printed the same text and wrote identical UTF-8 bytes containing a NUL and multi-byte code points. A mutation through a function call checked quietly and failed at runtime. Self-insertion, a parent-to-child back edge, and mutation of a borrowed fixed array kept their existing ML errors.
- The document and editor unit checks passed 10 of 10. The isolated VS Code 1.144.0 source and installed VSIX runs passed. The initial editor run exposed a warning parser that accepted Basic and Performance codes only. After extending it to recognize `TI-D001`, the rerun verified unsaved diagnostics, Unicode columns, hints, and clearing on edits or mode changes.

## Check-time measurements

The [benchmark CSV](ARC_TIKI_BENCHMARK.csv) records 11 samples per mode after warm-up on Windows x64. Modes used the same inputs in rotating order. All timings include process startup. The 2-file, 6-function workload measured 11.690 ms in Basic and 11.575 ms in Deep. The 21-file, 1,001-function workload measured 79.828 ms in Basic and 79.737 ms in Deep, with the same median peak working set of 26,378,240 bytes. Plain check measured 78.903 ms with the Stage 14 build and 78.520 ms with the new build on the larger graph.

A separate 2-file fixture measured 12.465 ms in Basic and 12.558 ms in Deep while emitting 50 warnings. The observed ranges overlap in all three comparisons. These samples did not resolve a consistent time difference between Basic and Deep. They measure source checks and do not measure generated program speed.

## Reproduction and remaining coverage

From the repository root on Windows x64, configure the pinned toolchains using the [build instructions](../../README.md#build-from-source). Use Node.js and the [isolated editor setup](../../tests/vscode/README.md) for the extension tests.

```powershell
./tools/Build-Windows.ps1 -Configuration Release -BuildDirectory build/arc-tiki-update/release
./tools/Build-Windows.ps1 -Configuration Debug -BuildDirectory build/arc-tiki-update/debug
node --test tests/docs/*.test.js tests/vscode/*.test.js
$env:MOULANG_TEST_COMPILER = (Resolve-Path 'build/arc-tiki-update/release/bin/moulang.exe').Path
node tests/vscode/run.js
```

For the timing comparison, use a separately built Stage 14 compiler as `--before`. Use the new Release compiler as `--after` and run the [benchmark command](../../tests/benchmarks/README.md) with `--deep --samples 11`.

The requested local suites ran on Windows x64. The hosted Windows runner and a fresh Windows machine remain untested for this stage. Rerun the commands above in a fresh Windows checkout to verify that environment. The minimum supported VS Code version 1.85 was outside the pinned editor run. To check it, point an isolated copy of `tests/vscode/run.js` at the 1.85 archive and adapt its CLI path detection if the archive has a different layout. The real play-button click remains a manual test. Open a test file in the isolated editor, set the new compiler path, click Run, and verify the terminal output. The native stale-handle cases exercise the instrumented Debug runtime. Generated LLVM IR was run through normal CTest coverage without AddressSanitizer.

## Changed files

- Compiler rule and flags in `include/moulang/tiki.hpp`, `src/semantic/tiki.cpp`, and `src/cli/main.cpp`.
- Editor setting and parsing in `vscode-extension/package.json` and `vscode-extension/src/diagnostics.js`.
- Regression coverage in `CMakeLists.txt`, `tests/integration/arc-tiki.ps1`, `tests/integration/tiki.ps1`, and `tests/vscode/suite.js`.
- Benchmark and reproduction in `tests/benchmarks/tiki.py`, `tests/benchmarks/README.md`, and `ARC_TIKI_BENCHMARK.csv`.
- Project and editor READMEs, `CHANGELOG.md`, `docs/LANGUAGE_SPEC.md`, `docs/USER_GUIDE.md`, `docs/IMPLEMENTATION_STATUS.md`, and the Stage 14 and Stage 15 design notes.
