# Moulang 0.3.0 preview

This Windows x64 preview bundles compiler 0.3.0, its native link kit, and Moulang for VS Code 0.1.6.

## Upgrade

Change explicit `string` declarations, including function signatures, to `str`. Install the bundled VSIX or update the existing `KaichuTheFox.moulang` extension to 0.1.6 when available. Point `moulang.executablePath` to the new compiler and keep its complete toolchain directory.

Compare the release ZIP's SHA-256 against the checksum asset before extracting it. Run the included `Verify-Install.ps1` to verify the extracted files and compile and run Hello World.

## Changes

- Concatenate, repeat, index, and slice UTF-8 strings. Indexing and slicing use Unicode code points.
- Print multiple values, query static types, and use Hangul identifiers and block comments.
- Format prompts and read typed input inside print blocks.
- Mutate shared `str[]` lists and ragged `str[][]` tables with checked indices.
- Read, replace, and append UTF-8 files with `RText()`, `WText()`, and `AText()`.
- Highlight the new syntax and suggest the new built-ins and list methods in VS Code.

Mixed-element lists remain a design proposal. New dynamic string and list storage stays allocated until process exit, under a shared 256 MiB limit. The release verifier restricts tool discovery on Windows; separate clean Windows 10 and 11 checks are tracked in the [release guide](RELEASE.md).
