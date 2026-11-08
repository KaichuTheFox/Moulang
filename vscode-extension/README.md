# Moulang for VS Code

Language support and native execution for Moulang (.m), the programming language from the HAZOS development team.

The extension recognizes .m files and provides syntax highlighting, snippets, completion, and compiler diagnostics. It uses a separate Windows x64 Moulang compiler.

1. Open the folder containing your .m file.
2. Set **Moulang: Executable Path** to the full path to moulang.exe if it is outside PATH.
3. Open a saved `.m` file and click the play button in the upper right of the editor, or run **Moulang: Run Current File** from the Command Palette. Output appears in the terminal and source diagnostics appear in **Problems**.

Enable **Moulang: Live Diagnostics** in a trusted workspace to check unsaved edits. This package uses the `KaichuTheFox.moulang` identity and `moulang.executablePath` setting. If you installed an earlier extension, set the compiler path again and disable the earlier extension to avoid duplicate file associations.

If the compiler cannot be found, choose **Open Settings** in the error notification and select `moulang.executablePath`. Use the path to the executable, without surrounding quotes. Keep the native compiler's complete bundled toolchain beside it.

## Compiler compatibility

- Extension 0.1.4 requires compiler 0.1.1. It adds compiler discovery, the settings action, and isolated extension tests.
- Extension 0.1.3 requires compiler 0.1.1. It introduces the current extension identity and editor play button.
- Compiler and extension versions are separate. A VSIX alone does not install the native compiler.

The current source revision additionally supports string escapes and highlights them in this extension. Escaped Unicode values and hexadecimal bytes are validated by the compiler. Previously downloaded compiler archives can predate this unreleased feature even when they share the development version number; build the current revision when testing the new syntax.

See the [compiler installation guide](https://github.com/KaichuTheFox/Moulang#install-without-building) and [issue tracker](https://github.com/KaichuTheFox/Moulang/issues). These URLs target the planned repository name; until the owner completes that rename, open the corresponding pages from the current repository.

The extension is licensed under MIT.
