# Stages 1 through 16 final verification

The update plan ends at Stage 16. The current source build has passed local Windows x64 validation. The package below is a local test build with compiler version 0.3.0 and extension version 0.1.6, the same version numbers as the earlier preview. Its SHA-256 identifies the new build.

## Delivered changes

- Stages 1 through 5 established the implementation audit, documentation index, Windows package checks, Git privacy procedure, and pre-ARC performance baseline. See the [starting audit](UPDATE_AUDIT.md) and [release verification guide](../RELEASE.md).
- Stages 6 through 10 added ARC for managed strings and collections, Debug memory diagnostics, source excerpts in compiler diagnostics, string and list traversal, shared integer lists with snapshot views, and checked narrowing for all integer destinations. See the [implementation status](../IMPLEMENTATION_STATUS.md) and [ARC verification](ARC_VALIDATION.md).
- Stages 11 through 13 added four standard modules and support for seven source suffixes, with `.mcx` as the default and `.m` compatibility. The existing source import and visibility rules gained mixed-suffix regression coverage. See the [language specification](../LANGUAGE_SPEC.md) and [module interface decision](MODULE_INTERFACE_DESIGN.md).
- Stages 14 through 16 added opt-in Tiki warnings, including the Deep warning for known aliases of lists under iteration, and improved non-ASCII indexing by scanning from the end selected by the index. The long Hangul benchmark fell from 247.378 to 106.455 ms. The [ARC and Tiki verification](ARC_TIKI_INTEGRATION_DESIGN.md#verification-results) and [performance report](PERFORMANCE_OPTIMIZATION_DESIGN.md#measurements-after-optimization) record measured tradeoffs and coverage.

## Local verification

- The Stage 16 Release and Debug builds passed all 27 and 24 CTest entries respectively. They include bounds diagnostics, ARC lifetimes, the Tiki runtime guard, `print()`, and block `format()`. The documentation and extension unit checks passed all 10 tests. The isolated VS Code 1.144.0 suites passed with both the source extension and installed VSIX.
- All 13 top-level example files ran successfully with the Stage 16 Release compiler. The packaged compiler and runtime hashes matched that build, and all 10 extension payload files matched the VSIX used in the isolated editor tests.
- The local Windows ZIP passed its SHA-256 check. The extracted verifier checked all 29 manifest entries, the required third-party notices, and the `.mcx` and `.m` Hello World examples with restricted development-tool discovery. The installed compiler also passed UTF-8 indexing with block formatting, `TI-D001` output, and the independent runtime guard on list mutation.
- Packaging ran the source-path privacy scanner on the distributed compiler, runtime, project text and VSIX entries, and the vendor linker. The pending source changes had no files at or above 50 MiB and no matches for the checked developer-home, token, and private-key patterns. The current local author and committer identities both use GitHub noreply addresses.

The local `moulang-windows-x64.zip` is 47,813,475 bytes. Its SHA-256 is `047758e67405593a4b7fbfc41f8172b87a8fba5f19bc0ac03e645f27944930f4`.

## Compatibility and follow-up

Checked integer narrowing can now report a runtime error where an older compiler kept the low bits. See the [cast migration examples](../USER_GUIDE.md#migrate-integer-narrowing-casts). Existing `.m` projects can keep their filenames. For users upgrading from 0.2.0, the [upgrade guide](../USER_GUIDE.md#migrate-from-moulang-020) explains the older `string` to `str` change.

The local test used a Windows development host with restricted tool discovery. Clean Windows 10 and 11 machines, the hosted Actions workflow for this source revision, and VS Code 1.85 remain unverified. Follow the [release verification guide](../RELEASE.md) for separate clean-machine testing. The next release needs a version decision and a clean-checkout CI run before publication.

The work is still in the local working tree. Godot integration has its own design work, beginning with the exported ABI and ownership rules described in the [roadmap](../ROADMAP.md). A separate design can also revisit sparse UTF-8 offsets if application measurements identify a bottleneck in random indexing.
