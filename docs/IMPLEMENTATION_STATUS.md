# Implementation status

## Unreleased UTF-8 indexing optimization

Non-ASCII string indexing scans from the front for nonnegative indices and from the back for negative indices. The runtime uses constant extra space and retains the original backing storage for each indexed result. ASCII indexing still uses direct offsets. See the [implementation and benchmarks](progress/PERFORMANCE_OPTIMIZATION_DESIGN.md).

## Unreleased ARC and Tiki integration

`check --tiki=deep` reports `TI-D001` for four structural list methods on a known alias of an active `foreach` source. The advisory rule is also available in All mode and VS Code Live Diagnostics. Existing runtime mutation checks cover aliases beyond the static pass. See the [rule, coverage, and measurements](progress/ARC_TIKI_INTEGRATION_DESIGN.md#deep-rule).

## Unreleased Tiki Inspector

Successful checks can opt in to Basic and Performance warnings. The initial rules cover unused locals, loop-carried string accumulation, and repeated indexing of known non-ASCII text. A rule can be disabled by code for one invocation. VS Code displays warnings from unsaved source through its optional live check. See the [rule definitions](progress/TIKI_INSPECTOR_DESIGN.md#initial-rules) and [usage](USER_GUIDE.md#run-tiki-inspector).

Release passed 26 CTest entries and Debug passed 23. Isolated VS Code source and installed VSIX suites passed. The [verification report](progress/TIKI_INSPECTOR_DESIGN.md#verification-results) records rule coverage, example review, and check-time measurements.

## Unreleased source module verification

Stage 13 uses the existing source import system for `.mcx`, `.m`, `.mxx`, `.mpp`, `.mcp`, `.m++`, and `.mc+`. A source module can be checked with `check --library` and used through its public declarations. New regressions exercise mixed-suffix transitive imports, file-local private functions and types, public name conflicts, and mixed-suffix cycles. See the [module interface decision](progress/MODULE_INTERFACE_DESIGN.md).

Release passed 25 CTest entries and Debug passed 22. The isolated VS Code source and installed VSIX suites passed, including a `.mcx` entry importing a `.m` source module.

## Unreleased source extensions

`.mcx` is the default in the CLI help and new examples. The compiler accepts relative imports across `.mcx`, `.m`, `.mxx`, `.mpp`, `.mcp`, `.m++`, and `.mc+`. VS Code associates all seven with the same language ID. The lexical import check requires the exact lowercase suffix. The filesystem resolves each accepted path using its own case rules.

Release passed 25 CTest entries and Debug passed 22. Isolated VS Code source and installed VSIX tests covered language association, snippets, execution, and unsaved diagnostics across all seven. The local ZIP passed checksum and extracted installation checks, including both Hello World examples. See the [extension design and verification](progress/SOURCE_EXTENSION_DESIGN.md).

## Unreleased standard modules

Current builds provide file-local standard imports for math, text, time, and random. Math checks integer absolute-value overflow and real domains. Text operations preserve Unicode code-point positions and use ARC for results. Time exposes UTC dates and a monotonic clock. Random uses a reproducible seed and samples the full signed integer range. See the [module reference](LANGUAGE_SPEC.md#standard-modules) and [design](progress/STANDARD_LIBRARY_DESIGN.md).

Release passed 24 CTest entries and Debug passed 21. Relocation and static runtime checks cover the standard module example. Debug reported zero remaining managed objects in the module integration test. VS Code unit tests and isolated source and installed VSIX suites passed.

## Unreleased checked integer narrowing

Explicit casts from wider integer types check the destination range for `char`, `short`, and `int`. The compiler reports failures at the cast location. Arithmetic wrap, implicit widening, and real conversions retain their established rules. See the [conversion design](progress/INTEGER_CONVERSION_DESIGN.md) and [migration guide](USER_GUIDE.md#migrate-integer-narrowing-casts).

Release passed 23 CTest entries and Debug passed 20. The checked narrowing suite exercises all six integer pairs at `-O0`, `-O2`, and `-O3`. VS Code unit tests and the isolated source and installed VSIX tests passed.

## Unreleased integer lists

The prefix declaration `int[] values` creates a shared mutable integer list. It supports indexed replacement, list methods, and guarded foreach. A read-only view retains the backing buffer and keeps the values from its creation. Writes detach a pinned buffer, and ARC reclaims each buffer after its last owner releases it.

The suffix declaration `int values[]` and untyped integer literals still use integer slices. Their standalone append returns an extended slice and preserves the input slice's contents. See the [collection guide](USER_GUIDE.md#choose-an-integer-list-or-slice) and [Stage 9 design](progress/INTEGER_LIST_DESIGN.md).

The Windows x64 Release and Debug builds passed 22 and 19 CTest entries, respectively. The integer list integration test checks aliases, bounds, iteration, nested and escaped views, typed literals, and legacy append. The Debug run reported zero remaining managed objects. VS Code unit tests and the isolated extension source and installed VSIX suites passed.

## Unreleased list iteration and ASCII indexing

String foreach walks UTF-8 code points using a forward byte cursor. Flat string list foreach yields a string and nested string list foreach yields a shared row. The list's active iterator count rejects structural changes at the operation, and its mutation version is checked when advancing. Indexed replacement is allowed. ARC retains each source and current item.

Validated strings record whether their UTF-8 bytes are all ASCII. ASCII indexing is constant time for both positive and negative indices. The compiler and runtime archive must be rebuilt together for the earlier internal ABI change. See the [iteration and indexing design](progress/FOREACH_DESIGN.md).

Release passed 21 CTest entries and Debug passed 18. Debug verified no remaining managed objects after the new positive cases and rejected an iterator with a stale version. The VS Code unit tests and isolated source and installed VSIX suites passed.

## Unreleased compiler diagnostics

Interactive compiler diagnostics show source excerpts with carets and selected explanations and hints. The first line preserves its existing location, ML code, and message. Redirected output remains single-line by default. The CLI supports explicit plain and rich output, and the semantic analyzer suggests a unique close match from visible variables and function names. See the [user guide](USER_GUIDE.md#read-diagnostics) and [Stage 7 design](progress/DIAGNOSTICS_DESIGN.md).

Release passed 18 CTest entries and Debug passed 15. Console mode checks and isolated VS Code source and VSIX suites passed. The diagnostic prose remains English. Korean explanation and hint catalogs are a future design.

## Unreleased ARC

Dynamic UTF-8 string storage and string lists use owning references. Contiguous string slices and indexed code points retain the backing buffer. Nested list slots retain their child lists. Compiler code generation releases owning locals on scope exit and transfers returned values to the caller. Class fields own their managed contents while the instance is alive.

Release passed 17 CTest entries and Debug passed 14. The ARC test covers one million bounded concatenations, repeated list operations, escaped values, class roots, and Debug leak and stale-handle diagnostics. It checks generated executables at -O0, -O2, and -O3. The VS Code unit tests and isolated source and installed VSIX suites passed. A native AddressSanitizer harness covered runtime string and list storage. The Moulang-generated LLVM IR did not use AddressSanitizer instrumentation.

The new compiler and runtime have an internal ABI change and must be rebuilt together. See the [ARC design](progress/ARC_DESIGN.md), [measured comparison](progress/ARC_VALIDATION.md), and [Debug memory checks](USER_GUIDE.md#check-managed-memory-in-a-debug-build).

## Published 0.3.0 language expansion

The current source implements `str`, string concatenation and repetition, Unicode code point indexing and slicing, multi-argument output, static `type()`, block comments, and Hangul identifiers. The [language specification](LANGUAGE_SPEC.md) defines their current source semantics. The published 0.2.0 preview remains a separate checkpoint.

Print blocks support checked brace formatting and Unicode line input into scalar variables. The integration suite exercises prompt ordering, redirected UTF-8, typed conversion, and malformed input and templates.

String lists share their identity across assignment and function calls. Their tests cover mutations and post-mutation blocks, escaping in list output, bounds and method errors, and the original standalone integer-slice append.

Nested string lists support ragged rows, child list access and replacement, and recursive rendering. Mixed-element lists remain a [design proposal](MIXED_LIST_DESIGN.md).

`RText()` and `WText()` use the existing read and replace operations. `AText()` uses Windows append access and accepts UTF-8 paths and data.

Local Windows x64 validation passed 16 Release and 13 Debug CTest entries. The VS Code unit suite, repository-link check, and isolated VS Code 1.144.0 source and installed-VSIX suites passed. The release also verified Hangul text through the VS Code task terminal. Clean Windows 10/11 installation remains a separate check. See the [current update audit](progress/UPDATE_AUDIT.md).

### Baseline audit before the language expansion

- Implemented. The lexer, semantic analyzer, LLVM emitter, and runtime supported UTF-8 literals with escapes and embedded NUL, the `string` type, byte-exact equality, single-argument scalar output, line comments, and `readText()` / `writeText()`. The extension highlighted the original spelling and supported compiler diagnostics.
- Partially implemented. Integer slices, fixed arrays, range views, and standalone `append(slice, value)` existed. The string operators, string lists, and the list method family were absent. String UTF-8 handling did not extend to identifiers, and comments were limited to one line.
- Missing. The `str` spelling, multi-argument output, static `type()`, nested or mixed lists, print-block input and formatting, and the short file operation names had no parser and runtime support.

## Earlier checkpoints

The published compiler 0.2.0 preview packages the original string spelling, long and overload rules, and extension 0.1.5. See the [0.2.0 release notes](RELEASE_NOTES_0.2.0.md). Earlier dated checkpoints below describe their original development versions.

Stage 6 implements the approved R2/P1/O1 rules: signed 64-bit long, checked long-to-int narrowing, explicit long/real mixing and ordinary same-file function overloads. Debug passes eight CTest entries. The new long suite covers boundaries, runtime sites, ABI flows and optimization levels.

## November 7 development checkpoint

On 2026-11-07, the local Release build passed eleven CTest entries and Debug passed eight, including the new byte-exact string escape suite. Extension unit checks passed six tests, including real TextMate/Oniguruma tokenization. Isolated downloaded VS Code 1.144.0 source and installed-VSIX suites passed, including escape diagnostics after a supplementary Unicode source character. The project URL/document-link regression check passed. The final stage 6 compiler 0.1.1 / extension 0.1.4 ZIP passed checksum, file manifest, and restricted-discovery Hello World verification, and its compiler hash matches the tested Release binary. Earlier package outputs are preserved.

Hosted Actions, a real Release upload, clean Windows 10/11 virtual machines, and manual editor-button rendering remain unverified. Stages 1 through 9 have completed their requested implementation or documentation scope locally; history replacement and repository rename remain owner operations. The owner approved [U1/X1/N1 string policies](STRING_ESCAPE_DESIGN.md). The owner approved R2/P1/O1 in [the integer design](LONG_DESIGN.md). Stages 7 through 9 are proposals/documentation only; Linux, improved diagnostic rendering, memory reclamation and Godot execution are not implemented.

## Stage 1 privacy and local relocation

- The owner selected the manual history rewrite plan and compiler-relative bundle. The guide includes private backups, recovery checks, a fresh mirror, an email-only mailmap, an explicit force-with-lease example, and post-push verification. The destructive commands remain for the owner to run.
- Distribution configurations omit compiler debug information, suppress linker debug records, and use a filename-only PDB reference. Supported MSVC toolchains also map source and build prefixes in object records and `__FILE__` values.
- The privacy scanner checks UTF-8, aligned and unaligned UTF-16LE, user-home patterns, and explicit forbidden prefixes. CTest includes scanner fixtures and a compiled `__FILE__` regression probe for supported distribution configurations.
- The compiler uses relative linker/library names. The build and local install copy its current link dependencies beside the executable. Compiler/runtime privacy gates and a relocation test with restricted discovery pass.
- The owner approved LLVM-MinGW 20260616 with LLVM 22.1.8 for Windows 10/11 x64. The runtime is separately compiled and statically linked into generated programs. The selected kit includes its third-party notices and no standalone Microsoft development libraries. Clean-Windows and release-package validation remain separate checks. See the [privacy review](PRIVACY.md).

## Available

- Stage 9 adds the ownership/borrow, reference counting and GC comparison, a provisional ARC recommendation and Godot boundary goals to ROADMAP.md. No memory reclamation or engine adapter is implemented. Allocation-site review and document-link checks validate the document only; runtime reclamation/engine tests remain future work.

- Stage 8 records ten actually collected compiler errors and proposed source spans, underlines and hints in DIAGNOSTIC_PROPOSALS.md. No diagnostic renderer change was made. Examples were checked using the current compiler; future rendering tests are specified separately.

- Stage 7 inventories OS dependencies and proposes TargetBackend, HostServices and PlatformRuntime boundaries in PLATFORM_DESIGN.md. No Linux implementation exists; validation is source inspection and document-link checks.

- The approved string escapes decode to validated UTF-8, support Unicode scalars through U+10FFFF, and preserve embedded NUL through byte-length values and I/O. Invalid escapes report ML1004/ML1005 with source coordinates; imports reject NUL. TextMate highlighting recognizes the escape forms. Existing source backslashes must be escaped explicitly. These rules are included in compiler 0.2.0; older development archives may predate them.

- The repository rename is prepared through a [file/link inventory and owner procedure](REPOSITORY_RENAME.md). Local link consistency checks cover project metadata and relative documentation targets. The actual GitHub rename, clone remote updates, hosted URLs, and external consumers remain owner tasks.

- Extension 0.1.4 uses the final display name and repository metadata, preserves the current extension/command/settings IDs, and resolves configured compiler paths before launching a task. A missing compiler offers an Open Settings action. Six unit tests and the downloaded VS Code 1.144.0 source/installed-VSIX host suites pass locally. Menu visibility is checked through the manifest; physical button rendering and the minimum declared VS Code version remain manual/unverified. See the [test guide](../tests/vscode/README.md).

- Windows CI and release workflow source now includes checksum-verified toolchain setup, a clean CMake build, CTest, VSIX/static-kit ZIP packaging, a separate restricted-discovery installation job, and tag-gated Release upload. Local job-body and package checks are distinct from an actual hosted Actions run and clean-OS verification. See the [release guide](RELEASE.md).

- Native Windows x64 compilation from UTF-8 .m source through LLVM IR and COFF to PE executables. The CLI provides --help, --version, run, build, and check.
- Local variables, type inference with var, arithmetic and comparison, short-circuit logic, and explicit numeric conversions. Numeric types include char, short, int, long, float, and double.
- Functions, explicit and inferred return types, conditionals, loops, break, and continue.
- Integer slices, append, checked indexing, zero-copy read-only views, and foreach.
- Value structs, positional initialization, nested fields, and field assignment.
- Nullable values, coalescing, safe member access, and limited flow analysis.
- Relative imports, file-level public and private visibility, UTF-8 file I/O, and LLVM optimization levels -O0, -O2, and -O3.
- VS Code highlighting, completion, snippets, execution, compiler diagnostics, and opt-in diagnostics for unsaved edits.

## Limited support

- Local fixed integer arrays support value copies and local view restrictions.
- Classes support reference semantics, constructors, access control, nullable calls, and limited single inheritance.
- Class objects and heap-backed slice buffers live until process exit.

## Future work

- Arrays and slices of other types, general lifetime and ownership analysis, method overriding, and module aliases.
- Garbage collection, exceptions, multiple inheritance, generics, unions, pointers, unmanaged memory, and concurrency.
- A language server, source-level debugger, Windows installer, and a larger standard library.

See the [compiler integration suite](../tests/integration/compiler.ps1) and the [VS Code host suite](../tests/vscode/suite.js) for the scenarios covered by automated tests.
