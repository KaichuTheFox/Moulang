# Moulang 0.4.0 preview

This Windows x64 preview bundles compiler 0.4.0, its native link kit, and Moulang for VS Code 0.2.1. The extension uses the `KaichuTheFox.moulang` identity. Both MIT notices list `KaichuTheFox` and `Team.HaZos`.

## Changes

- Reclaim dynamic strings and lists with ARC, including shared string buffers and integer-list snapshot views. Debug builds report remaining managed objects and reject stale managed handles. Class objects and legacy heap integer slices retain process lifetime.
- Iterate over strings and shared lists with `foreach`. A list permits indexed replacement during traversal and rejects structural changes through any alias.
- Use `.mcx` for new programs. Existing `.m` files and five additional lowercase suffixes work in the compiler and VS Code. Source imports can mix the supported suffixes.
- Import the math, text, time, and random standard modules. Compiler errors can show source excerpts, carets, and selected hints.
- Enable Tiki Inspector Basic, Performance, or Deep warnings in `check` or VS Code Live Diagnostics. Deep flags supported cases of structural list changes through known aliases. The runtime applies its own iteration checks.
- Speed up indexing of long UTF-8 strings by scanning from the end selected by the index. See the [benchmark results](progress/PERFORMANCE_OPTIMIZATION_DESIGN.md#measurements-after-optimization).

## Upgrade and install

Explicit narrowing between integer types raises a runtime error when the value exceeds the destination range. For example, `char(260)` previously produced `4`. Write `char(value & 255)` to keep the low eight bits intentionally. See the [cast migration guide](USER_GUIDE.md#migrate-integer-narrowing-casts) for signed examples. Existing `.m` projects can keep their filenames.

Compare the ZIP checksum, extract the full compiler bundle, and run the included `Verify-Install.ps1`. Install the included `moulang-0.2.1.vsix` in VS Code. If the compiler is outside PATH, update `moulang.executablePath` to the extracted `bin/moulang.exe`. Keep the runtime archive and native link kit alongside it. See the [installation guide](INSTALL.md).
