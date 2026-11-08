# Local work report

Date: 2026-11-07. Stages 1 through 9 have completed their requested implementation or documentation scope locally. The owner approved R2/P1/O1 before stage 6. Stages 7 through 9 intentionally contain proposals only. All changes remain uncommitted local work. No remote history rewrite, force push, repository rename, Release upload, or Marketplace publication has been performed.

## Completed scope

1. Privacy: documented private backups, recovery, email-only filter-repo rewriting, explicit remote leases, limitations of public-email removal, and GitHub email protection settings. Applied distribution path mapping/debug metadata controls and relative tool discovery. The owner approved the LLVM-MinGW static runtime kit to replace standalone Microsoft development libraries; exact archive digests and required license notices are retained.
2. Windows build and release: added checksum-verified toolchain installation, a localized-MSVC-aware CMake build wrapper, pinned Actions, CTest, extension checks, complete ZIP packaging, per-file hashes, a separate restricted-discovery installation job, and tag-gated Release upload. Documented no-build installation and separate clean-Windows verification.
3. Extension: prepared version 0.1.4 for the current compiler development version 0.1.1, final display name and repository metadata, executable discovery and an Open Settings action, downloaded/isolated VS Code testing for source and VSIX payloads, and a compiler compatibility mapping. Existing current IDs and settings are preserved.
4. Repository naming: inventoried README, docs, extension metadata, test assertions, workflow contexts, future badges, external consumers, and clone remotes. The owner-only rename guide leaves server and local remote changes manual.
5. Strings: implemented the owner's U1/X1/N1 choice. The lexer decodes supported escapes and validates UTF-8; braced Unicode accepts scalar values through U+10FFFF. Explicit-length LLVM constants preserve NUL, and imports reject NUL paths. Added exact-byte native tests, real TextMate tokenization tests, and editor diagnostic checks after a supplementary Unicode character.

6. Long: implemented signed 64-bit values throughout literals, LLVM/runtime operations, printing and ABI flows; checked long-to-int narrowing, explicit long/real mixing and ordinary same-file overloads. Added tests at all optimization levels and preserved independent int-wrap coverage.
7. Platform: inventoried Windows dependencies/backslashes and proposed host, target and runtime boundaries. No Linux code was added.
8. Diagnostics: collected ten real compiler messages and proposed source underlines, clearer explanations and hints. No renderer was changed.
9. Memory: compared ownership/borrow, ARC and GC; documented persistent heap allocations and a provisional ARC/engine-boundary roadmap. No reclamation or Godot code was added.

## Validation

- Release configuration/build and eleven CTest entries passed. Debug configuration/build and eight CTest entries passed. These retain the existing compiler suite and cover relocation, static system-DLL imports, toolchain/package integrity, privacy scanning, compiled path probes, and the new string suite.
- The native string suite covers run/check/build, all supported escapes, exactly two-digit hexadecimal consumption, Unicode boundary values, 24 malformed/invalid UTF-8 cases with exact coordinates, truncated escapes, raw NUL rejection, escaped imports, NUL path rejection, and byte-for-byte output/file contents at -O0, -O2, and -O3.
- Six extension unit tests passed, including three actual TextMate/Oniguruma tests. Downloaded VS Code 1.144.0 source and installed-VSIX payload suites passed activation, command/menu declarations, current-file arguments after switching editors, completion/snippets, native execution, compiler diagnostics, unsaved edits, and escape-error column conversion. The user-installed VS Code executable and profiles were not used.
- Project-owned repository URLs and relative Markdown targets passed the link regression test. Final workflow YAML parsed successfully. This does not establish remote URL availability or an actual GitHub Actions run.
- Release compiler, runtime, and bundled linker scans reported zero user-home and repository-prefix matches. Original supplier static archives still contain public upstream CI paths; those are disclosed in PRIVACY.md and have not been silently removed. Full supplier attribution and public copyright emails remain intact.
- The stage 5 ZIP passed its checksum, 28-file manifest, and restricted-discovery Hello World check. It contains the new string compiler and extension 0.1.4. Its size is 47,722,053 bytes (45.51 MiB); SHA-256 is d556bff58a826c5797dbe107f49c8240573b83d19c1ca140b77b70f174b68e86. It is retained under build/stage5-release-package alongside the separate checksum; the older stage 4 output is also preserved. All package outputs remain ignored by Git.
- Text-source policy scanning found no literal Korean text, former project name, real developer home paths, or recognized GitHub-token/private-key patterns. The privacy test intentionally contains synthetic path samples. Binary image bytes are not treated as source text. No Git-candidate file is at least 50 MiB; the current source/config/docs collection is below 1 MiB, excluding Git history and ignored toolchains/build outputs.

## Problems found and resolved

- MSVC source path mapping required native Windows prefix spelling. Linker debug/PDB flags alone did not remove configured absolute tool paths, so tool names are now relative to the compiler.
- Direct no-CRT static linkage lacked startup support. The approved GNU-flavor PE linkage now uses selected CRT startup objects and a separately compiled C ABI runtime, preserving tested outputs and system-only DLL imports.
- Localized MSVC include prefixes caused Ninja tracking problems. The build wrapper probes the actual prefix rather than assuming English output.
- New vsce requires a single packaging inventory; .vscodeignore now controls it. Packaging invokes npm.cmd so PowerShell does not consume pass-through options.
- The extension test harness retained a closed preview-editor handle after switching files and tried removing a directory still held by a task terminal. It now reacquires the editor and retains generated fixtures without weakening assertions.
- Recent VS Code archives use versioned resource directories. The runner obtains cli.js from the archive's own launcher and invokes it directly with argument arrays, preserving Unicode and spaces.
- A local sandbox initially denied linking under its default temporary directory. The package verification passed with a writable test temporary directory. This is disclosed instead of being presented as a clean-OS result.

## Unverified and manual work

- Execute the private backup/history procedure and GitHub email setting changes personally; existing public copies of an email cannot be reliably erased.
- Perform the GitHub repository rename and clone remote updates personally. The final-name download/source/issue links need live checks after that change.
- Run the committed workflow on GitHub, verify actual tag permissions and Release upload, and test the ZIP on clean Windows 10 and Windows 11 x64 with no development toolchains. The local host contains Visual Studio, even though installation verification restricts discovery.
- Manually check play-button rendering/click behavior. The menu declaration and its command execution are automated; physical UI rendering and the declared minimum VS Code 1.85 engine have not been tested.
- Select the final release version/tag before publishing unreleased language changes. Existing archives with the same development version may not include the new string syntax. Preserve previous releases and do not overwrite tags to hide this distinction.
- Remaining future decisions: Linux target/libc baseline, diagnostic rendering/protocol details, ARC versus another memory model, cycle handling, threading and the Godot ABI/version contract. R2/P1/O1 and U1/X1/N1 are already approved and implemented.


## Final package verification

The latest package is build/stage6-final-package/moulang-windows-x64.zip, 47,726,589 bytes (45.52 MiB), SHA-256 80f013213ea711433577f6b8db7779ca5319505ff7b4df444907e13f6ffb0a2e. It contains the current 64-bit-long compiler and runtime plus extension 0.1.4. Checksum, complete file manifest and restricted-discovery Hello World passed after extraction. The installed compiler matches the tested Release binary. Prior stage 4/5 packages remain preserved and predate these integer changes.

## Additional stage 6 verification

The long suite also passed exported overload selection, private-overload rejection and cross-module public-name conflict checks. Stage 6 source/VSIX host tests and all six extension unit tests passed. Platform, diagnostic and memory documents passed relative-link checks; no Linux, new diagnostic renderer, collector or Godot binary was tested.

## Changed and added files

- `.github/workflows/windows.yml`
- `CHANGELOG.md`
- `cmake/toolchain.hpp.in`
- `CMakeLists.txt`
- `docs/COMPILER_ARCHITECTURE.md`
- `docs/DEPLOYMENT.md`
- `docs/DIAGNOSTIC_PROPOSALS.md`
- `docs/IMPLEMENTATION_STATUS.md`
- `docs/INSTALL.md`
- `docs/LANGUAGE_SPEC.md`
- `docs/LONG_DESIGN.md`
- `docs/PLATFORM_DESIGN.md`
- `docs/PRIVACY.md`
- `docs/RELEASE.md`
- `docs/REPOSITORY_RENAME.md`
- `docs/ROADMAP.md`
- `docs/STRING_ESCAPE_DESIGN.md`
- `docs/USER_GUIDE.md`
- `docs/WORK_REPORT.md`
- `include/moulang/ast.hpp`
- `include/moulang/types.hpp`
- `README.md`
- `runtime/src/real.cpp`
- `runtime/src/runtime.cpp`
- `runtime/src/startup.cpp`
- `src/codegen/llvm/object.cpp`
- `src/driver/windows.cpp`
- `src/lexer/lexer.cpp`
- `src/parser/parser.cpp`
- `src/semantic/semantic.cpp`
- `src/source/program.cpp`
- `tests/docs/repository-links.test.js`
- `tests/integration/artifact-privacy.ps1`
- `tests/integration/compiler.ps1`
- `tests/integration/install-integrity.ps1`
- `tests/integration/long.ps1`
- `tests/integration/privacy-source.cpp`
- `tests/integration/relocation.ps1`
- `tests/integration/static-runtime.ps1`
- `tests/integration/string-escapes.ps1`
- `tests/integration/toolchain-integrity.ps1`
- `tests/vscode/compiler.test.js`
- `tests/vscode/grammar.test.js`
- `tests/vscode/README.md`
- `tests/vscode/run.js`
- `tests/vscode/suite.js`
- `third_party/licenses/compiler-rt-LICENSE.txt`
- `third_party/licenses/libcxx-LICENSE.txt`
- `third_party/licenses/libunwind-LICENSE.txt`
- `third_party/licenses/LLVM-LICENSE.txt`
- `third_party/licenses/MinGW-COPYING.txt`
- `third_party/licenses/MinGW-runtime-COPYING.txt`
- `third_party/README.md`
- `tools/Build-Windows.ps1`
- `tools/Check-ArtifactPrivacy.ps1`
- `tools/Install-Toolchains.ps1`
- `tools/Package-Windows.ps1`
- `tools/toolchains.json`
- `tools/Verify-Install.ps1`
- `vscode-extension/.vscodeignore`
- `vscode-extension/package-lock.json`
- `vscode-extension/package.json`
- `vscode-extension/README.md`
- `vscode-extension/src/compiler.js`
- `vscode-extension/src/extension.js`
- `vscode-extension/syntaxes/moulang.tmLanguage.json`
