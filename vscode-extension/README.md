# Moulang for VS Code

Language support and native execution for Moulang (.m), the programming language from the HAZOS development team.

The extension recognizes .m files and provides syntax highlighting, snippets, completion, and compiler diagnostics. It uses a separate Windows x64 Moulang compiler.

Extension 0.1.6 highlights `str`, Hangul names, block comments, print-block items, string list operations, and file I/O aliases. Use compiler 0.3.0 for these features. Compiler 0.2.0 uses `string` in explicit declarations.

## Install the compiler

Download the [Moulang 0.3.0 compiler ZIP](https://github.com/KaichuTheFox/Moulang/releases/download/v0.3.0/moulang-windows-x64.zip) and its [SHA-256 file](https://github.com/KaichuTheFox/Moulang/releases/download/v0.3.0/moulang-windows-x64.zip.sha256). Compare the ZIP's `Get-FileHash -Algorithm SHA256` result before extracting the whole package, for example to `C:\Moulang`. Keep `bin/toolchain` and its notices. The kit includes the tools needed to build and run Moulang programs.

Set `moulang.executablePath` to `C:\Moulang\bin\moulang.exe`, without quotes, or add the bin folder to PATH and restart VS Code. The extension does not install or download a compiler automatically. Run the extracted `Verify-Install.ps1` to check package hashes and execute Hello World.

1. Open the folder containing your .m file.
2. Set **Moulang: Executable Path** to the full path to moulang.exe if it is outside PATH.
3. Open a saved `.m` file and click the play button in the upper right of the editor, or run **Moulang: Run Current File** from the Command Palette. Output appears in the terminal and source diagnostics appear in **Problems**.

Enable **Moulang: Live Diagnostics** in a trusted workspace to check unsaved edits. This package uses the `KaichuTheFox.moulang` identity and `moulang.executablePath` setting. If you installed an earlier extension, set the compiler path again and disable the earlier extension to avoid duplicate file associations.

If the compiler cannot be found, choose **Open Settings** in the error notification and select `moulang.executablePath`. Use the path to the executable, without surrounding quotes. Keep the native compiler's complete bundled toolchain beside it.

## Compiler compatibility

- Extension 0.1.6 pairs with compiler 0.3.0 for the new syntax and completion.
- Bundled extension 0.1.5 pairs with compiler 0.2.0 and updates download and setup guidance.
- Existing extension 0.1.4 can invoke compiler 0.2.0 by updating its executable path. It originally targeted compiler 0.1.1 and added compiler discovery and the settings action.
- Extension 0.1.3 requires compiler 0.1.1. It introduces the current extension identity and editor play button.
- Compiler and extension versions are separate. A VSIX alone does not install the native compiler.

Compiler 0.2.0 supports the documented string escapes, signed 64-bit long and ordinary function overloads. This extension highlights escapes; the compiler validates Unicode and decoded UTF-8. Earlier development archives may predate these features. See the [0.2.0 release notes](https://github.com/KaichuTheFox/Moulang/releases/tag/v0.2.0).

See the [compiler installation guide](https://github.com/KaichuTheFox/Moulang#install-without-building) and [issue tracker](https://github.com/KaichuTheFox/Moulang/issues) in the Moulang repository.

The extension is licensed under MIT.
