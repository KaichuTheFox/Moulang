# Changelog

## Unreleased

No changes scheduled after the 0.2.0 preview yet.

## Moulang 0.2.0 preview and bundled VS Code extension 0.1.5 - 2026-11-08

- Publish the Windows x64 compiler kit as v0.2.0 with ZIP and SHA-256 assets after verified CI build and installation jobs.
- Update installation and extension documentation with versioned compiler downloads and the renamed Moulang repository.
- Keep previous releases and development checkpoints intact. The bundled 0.1.5 VSIX updates installation guidance; Marketplace publishing is separate.

- Added a memory-lifetime limitation review, ownership/ARC/GC comparison, provisional ARC recommendation and native-engine integration milestones. No memory model or Godot code was changed.

- Recorded ten current error examples and proposed clearer wording, source underlines and hints. This is a design document; existing diagnostic rendering remains in use.

- Added a Windows dependency/backslash inventory and host/target/runtime separation proposal for future ELF support. No platform refactor was implemented.

- Implemented approved R2/P1/O1 integer rules: signed 64-bit long, int/long literal inference, full-width runtime/LLVM operations, checked long-to-int narrowing, explicit long/real mixing, and ordinary same-file function overloads with per-argument ranking and ambiguity errors. Updated obsolete 32-bit-long expectations while retaining int wrap coverage and added long boundary/ABI/optimization/error tests.
- Implemented the approved string escape policy: control/quote/backslash/NUL escapes, exactly two-digit hexadecimal bytes with strict UTF-8 validation, and braced Unicode scalar escapes. Preserved embedded NUL through byte-length code generation, comparison, and file/output operations; rejected NUL-containing imports. Added byte-exact native/optimization tests, precise invalid-escape diagnostics, and real TextMate/Oniguruma grammar tests. Existing literal backslashes must now be escaped.

- Added the owner-only repository rename checklist, an inventory of documentation/extension/workflow/remote updates, and project URL and relative-document-link regression checks. No repository or remote name was changed.

- Prepared extension 0.1.4 for compiler 0.1.1: final display name and repository metadata, executable lookup with a missing-compiler settings action, and isolated downloaded VS Code tests for source and packaged extension payloads. Preserved the current extension ID, language ID, run command, and settings keys. No Marketplace upload was performed.

- Added checksum-verified Windows CI, static-kit/VSIX ZIP packaging, per-file installation verification, and a separate installation job. Version-tag releases require successful build and installation jobs. Documented clean-Windows checks beyond the hosted runner.

- Added a manual privacy guide for backups, owner-approved email rewriting, safe ref replacement, and GitHub email protection settings.
- Hardened Release and MinSizeRel debug metadata settings and added capability-checked source/object path mapping.
- Added privacy scanner fixtures, compiler/runtime artifact gates, and a compiled `__FILE__` regression test.
- Replaced compiled-in absolute tool paths with compiler-relative dependency names. Added build/install bundles and relocation tests with restricted discovery. Replaced standalone Microsoft development libraries with the approved LLVM-MinGW 20260616 static kit for Windows 10/11 x64. Pinned archive digests, preserved third-party notices, and added system-only DLL import checks.

## VS Code extension 0.1.3

- Added a play button to saved `.m` editors. It runs the active source file using the same command as the Command Palette.
- Aligned the extension identity, command and settings keys, language identifier, and syntax file names with Moulang. The new extension installs separately from earlier versions under the previous identity.
- Documented compiler setup and the new run workflow.

## Moulang 0.1.1 and VS Code extension 0.1.2

- Adopted the Moulang name and logo while keeping the `.m` format and compiler commands.
- Added an English build and deployment guide and packaged the new icon for VS Code.
