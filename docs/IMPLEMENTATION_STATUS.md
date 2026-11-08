# Implementation status

Compiler 0.2.0 preview packages the implemented string/long/overload rules and extension 0.1.5. The owner completed the repository rename. Main-branch CI for commit dce376b passed build and installation verification; the 0.2.0 tagged workflow must pass again before publication. See [0.2.0 release notes](RELEASE_NOTES_0.2.0.md). Earlier dated checkpoints below describe their original development versions.

Stage 6 implements the approved R2/P1/O1 rules: signed 64-bit long, checked long-to-int narrowing, explicit long/real mixing and ordinary same-file function overloads. Debug passes eight CTest entries. The new long suite covers boundaries, runtime sites, ABI flows and optimization levels.

## Current verification and next approval

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
