# Stage 14 Tiki Inspector

Status is implemented under approved policy B.

Tiki Inspector is a static analyzer built into the Moulang compiler by HAZOS under the project's MIT license. It uses resolved Moulang types, lexical bindings, and source locations.

## Behavior before Stage 14

The current Windows x64 compiler accepted an unused local, a string accumulator loop, and a non-ASCII indexed traversal without warnings. It rejected `--tiki=perf` with usage exit code 2. The same compiler reported ML3022 for an unchecked nullable struct receiver and ML3033 for writing a fixed array while a view borrowed it. Those checks already run in ordinary compilation.

The compiler checks the source graph, including imported modules, before emitting code. It already had plain and rich diagnostic formats with one-based Unicode code-point columns. Before Stage 14, both the VS Code task matcher and the unsaved-source diagnostic provider handled ML errors only. The extension sends unsaved text to `moulang check --stdin --library`.

## A. Enable a small default rule set

Add a low-cost unused-local warning to every successful `check`. Keep Performance and future Deep rules opt-in. This makes the first warning easy to discover. Existing scripts that expect empty standard error on successful checks and editors using live checks would start receiving warnings.

## B. Start with opt-in rules

Keep existing compiler checks active. Run additional rules through `moulang check` only when requested. Offer two initial rule groups, Basic and Performance. A shared, read-only pass visits the typed AST after semantic analysis succeeds. It uses resolved types and lexical bindings. Keep all warnings advisory. Existing ML errors and runtime checks continue to enforce current safety rules.

- `moulang check main.mcx --tiki=basic` runs the low-cost rule group alongside normal checks.
- `moulang check main.mcx --tiki=perf` runs the performance group.
- `moulang check main.mcx --tiki=all` runs every available group. The first release includes Basic and Performance.
- `--tiki-disable=TI-B001,TI-P002` disables named rules for one check. Accept it with an enabled mode and reject unknown IDs or malformed values with a usage error. Keep source-level suppressions as a later decision.
- Deep is a later group for data-flow and ownership work. `--tiki=deep` becomes available once there are Deep rules. Ordinary `check`, `run`, and `build` use their existing behavior.

Policy B is implemented. Ordinary successful checks preserve their existing empty standard error.

## Initial rules

1. **TI-B001, unused local.** Report a local declaration with no read in its lexical scope, including nested blocks. Analyze binding identity. Assignments and input targets are writes. Passing a value, reading it in a condition, using it in an index, or using its type in `type(value)` counts as use. Leave parameters, fields, and explicitly ignored underscore-prefixed locals outside the initial rule. The warning suggests naming an intentionally unused value with an underscore or reviewing whether the initializer's side effects are needed.
2. **TI-P001, repeated string accumulation.** Find a loop-carried `str` binding updated directly by `text = text + part` or its compound form. The old value must be a direct operand, and the binding must be declared outside the repeated region. Skip cases with a statically empty added literal and loops proven to run at most once. The warning describes possible repeated copying of a growing string. If only the final result is used and evaluation order can be preserved, collecting pieces and using `std:text`'s `join` is a candidate fix. ARC reclaims intermediates when their last owner is released.
3. **TI-P002, repeated indexed UTF-8 traversal.** Recognize a zero-based increasing loop whose bound is `std:text` length of a local string initialized to a known non-ASCII literal with at least two code points. Require the same binding for the indexed access and the length and the loop index for the subscript. The first implementation conservatively skips the finding if either binding is assigned or targeted by input anywhere in its scope. Recognize the resolved standard export through any alias. The warning suggests `foreach` when the program only needs successive code points. ASCII inputs already have constant-time indexing and are outside this rule.

Warn at the declaration or operation once per source location and rule. A performance warning identifies a source pattern and explains its likely cost. Actual execution cost depends on data, control flow, and optimization.

## Diagnostics and VS Code

Use a machine-readable first line such as `main.mcx:8:5: warning TI-P001: Repeated concatenation can copy a growing string.` Rich output adds a source span, the reason it matters, and a concise suggestion. Preserve the current ML error lines. In plain output the message carries the reason and suggestion in one line. Report warnings on standard error, with exit code 0 when compilation checks succeed. ML compile errors retain exit code 1 and take precedence over the analysis pass.

Add an opt-in `moulang.tikiMode` setting to the existing trusted-workspace live diagnostics flow, with off as the default. Reuse the unsaved input overlay and its cancellation and document-version checks. Extend diagnostic parsing to recognize `warning TI-...` and display it as Warning under Tiki Inspector. CLI checks can report warnings in imported files at their actual paths. The live provider continues to show only diagnostics for its current document to avoid stale warnings across different overlays. Verify that the usual Run command still handles ML errors through its existing task matcher.

## Deep analysis and ARC

Stage 15 adds the first Deep rule. See the [ARC integration design and validation](ARC_TIKI_INTEGRATION_DESIGN.md#deep-rule) for shared-list mutation diagnostics and their limits.

Start later work with bounded intraprocedural data-flow and alias analysis. Reuse the existing nullability and borrow rules for unchecked access and initialization. Consider warnings for potentially surprising writes through shared list bindings and for ownership patterns where the runtime representation makes the issue observable. Record unsupported or inconclusive cases instead of drawing conclusions about them.

ARC currently reclaims managed string and list storage and integer-list snapshot buffers. String slices can retain a shared backing buffer. Class instances and legacy heap-backed integer slices have process lifetime. Nested managed string lists descend in element type and cannot point back to their parent. Strong ownership cycle rules require a type that can form a cycle in the managed ownership graph. Revisit them with that runtime design. Stale handles and residual live objects observed by the Debug runtime remain runtime evidence.

## Validation and rule promotion

- Prepare positive and negative fixtures per rule before implementation. Cover nested scopes, writes and reads, deliberate ignored locals, safe nullable accesses, loop-local expressions, small loops, known ASCII, known non-ASCII, and imported declarations. Require every defined positive fixture to warn at the intended span, with zero warnings in the corresponding negative fixtures.
- Review findings on repository examples and a labeled set of independent or representative valid programs. Record true positives, false positives, and missed in-scope cases per rule. Consider enabling a rule by default only after manual review confirms at least 95 percent precision on at least 20 reviewed findings, all boundary fixtures pass, and the compatibility impact is approved separately. If the sample is smaller, keep it opt-in.
- Measure plain `check` before and after implementation, and compare off, Basic, and Performance on the same compiler, source graph, and hardware. Use a warm-up and at least seven timed runs per case. Record elapsed time and peak working set, including a larger multi-file fixture. Review any default-check median regression over 5 percent outside run-to-run variation before shipping. Report opt-in overhead separately.
- Compare the pre-ARC compiler's baseline diagnostics on shared compatible fixtures with the current compiler. The pre-ARC build has no Tiki warnings. Compare ARC resource behavior separately using Release/Debug fixtures and Debug reports. Only compare Tiki counts between builds that both implement the same rules on the same input.
- Test stable rule codes, warning and error exit codes, Unicode columns, plain and rich output, filter switches, the VS Code Problems severity and stale-result handling, and unaffected `print()` and block `format()` behavior. Rerun Release and Debug CTest and isolated VS Code source and installed VSIX suites after implementation.

## Verification results

Release passed 26 CTest entries and Debug passed 23. The targeted Tiki cases passed in both configurations. The isolated VS Code source and installed VSIX suites verified warning severity, Unicode columns, opt-in settings, and warning removal after editing or disabling the mode. The existing print-block and compiler error tests passed.

The labeled regression fixtures yielded four expected unused-local warnings, four expected accumulation warnings across the direct and nested loop cases, and one expected indexed traversal warning. The negative fixture emitted zero warnings. A separate Unicode fixture verified the warning location and format. All 14 repository examples and source modules passed with zero warnings in All mode. This is a local coverage check. There is no independent precision estimate, and the rules remain opt-in.

The pre-ARC 0.3.0 compiler and the current compiler agreed on the exit codes and ML diagnostics for three shared fixtures. They cover a valid integer loop, unchecked nullable access, and a write through a borrowed fixed-array owner. The full CTest suites also exercised the established ARC fixtures. The [ARC results](ARC_VALIDATION.md) record the earlier resource comparison. A cross-version Tiki warning comparison requires builds with the same warning rules.

The [benchmark CSV](TIKI_BENCHMARK.csv) reports seven timed runs after warm-up per case, including compiler startup, on Windows x64. The 21-file, 1,001-function case took 77.3 ms before Tiki and 81.0 ms with Tiki off. Basic took 79.0 ms, Performance 75.7 ms, and All 77.3 ms on the new build. The observed ranges overlap. These results do not isolate analysis cost or show that enabling a mode improves speed. The two-file case took about 12.3 to 12.7 ms across builds and modes. Median peak working set for the larger case stayed near 19.7 MB. Both inputs completed without printing warnings.

## Changed files

- Inspector and CLI in `include/moulang/tiki.hpp`, `src/semantic/tiki.cpp`, `src/cli/main.cpp`, and `CMakeLists.txt`.
- VS Code setting, diagnostic parsing, and integration tests in `vscode-extension/package.json`, `vscode-extension/src/diagnostics.js`, and `tests/vscode/suite.js`.
- Compiler fixtures and measurements in `tests/integration/tiki.ps1`, `tests/benchmarks/tiki.py`, `tests/benchmarks/run.py`, and `TIKI_BENCHMARK.csv`.
- Usage, status, and changelog in `docs/USER_GUIDE.md`, `docs/LANGUAGE_SPEC.md`, `docs/IMPLEMENTATION_STATUS.md`, `docs/README.md`, `vscode-extension/README.md`, `tests/benchmarks/README.md`, and `CHANGELOG.md`.
