# Stage 12 source extension design

Status is approved and implemented.

## Supported extensions

- **`.mcx`** is the official default for new sources, examples, and documentation.
- **`.m`** remains supported for existing Moulang projects.
- **`.mxx`** is an additional source suffix.
- **`.mpp`** stands for Moulang Plus Plus.
- **`.mcp`** stands for Moulang Common Program.
- **`.m++`** is an additional source suffix with two literal plus characters.
- **`.mc+`** is an additional source suffix with one literal plus character.

All seven suffixes identify the same language. They use the same source grammar, type rules, compiler, runtime, and VS Code language ID. A suffix does not select a dialect or change code generation. The support list and extension manifest contain seven entries using lowercase letters.

## Windows verification before Stage 12

The compiler already accepted explicitly named entry files regardless of their suffix. Relative source imports required a lowercase `.m` suffix. Standard imports such as `import "std:math"` used a separate resolver. The VS Code manifest contributed only `.m` to the `moulang` language ID.

On Windows x64, the current compiler passed `check`, `run`, and `build` with the expected output name for all seven suffixes as explicit entry paths. The `.m++` and `.mc+` probes also used filenames with spaces. Relative imports worked for `.m` and reported ML3028 for the other six. In the test directory, the CLI opened a lowercase file when passed its uppercase `.M` path. A lowercase import also resolved a file created with an uppercase suffix. A relative import explicitly ending in `.M` still reported ML3028.

## CLI and imports

Document `moulang run main.mcx`, `check main.mcx`, and `build main.mcx` as the default usage. Retain the existing behavior for explicitly named CLI paths. The CLI does not search for suffix alternatives when the named file is missing. Default build output replaces the input suffix with `.exe`. When two input files share a stem, `-o` can select different output paths.

Use the seven exact spellings as the relative import allowlist. For example, `main.mcx` can import `helper.m` and `main.m` can import `helper.m++`. Treat the plus characters as part of the path, as in `import "modules/helper.mc+"`. Imports name actual files with their suffixes. Preserve existing relative resolution, cycle and duplicate checks, standard module IDs, and the ML3028 diagnostic code for unsupported import suffixes.

## Case handling

Keep the lexical import check case-sensitive on every platform. `import "helper.M"` is rejected because `.M` is outside the lowercase allowlist. After a lowercase import passes the suffix check, the OS resolves the path. On a case-insensitive Windows directory, `import "helper.m"` may resolve a file physically named `helper.M`. A case-sensitive directory requires the filename spelling to match. Use lowercase physical filenames for portable projects. Preserve filesystem-based deduplication if two spellings resolve to the same file. [Windows case sensitivity](https://learn.microsoft.com/en-us/windows/wsl/case-sensitivity).

All seven suffixes fit ordinary Windows, Linux, and macOS filename conventions. The compiler tests described here ran on Windows. PowerShell filename filters matched `*.m++` and `*.mc+` as literal suffixes. For regular expressions use case-sensitive matching and escape each plus, as in `\.(mcx|m|mxx|mpp|mcp|m\+\+|mc\+)$`. [Windows filename rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file), [Linux pathname manual](https://man7.org/linux/man-pages/man7/pathname.7.html), [Apple file and path conventions](https://developer.apple.com/documentation/technologyoverviews/files-and-directories), [PowerShell regex syntax](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_regular_expressions).

## VS Code

Register all seven suffixes as literal entries under the existing `moulang` language contribution. The extension keeps its identity, grammar, snippets, settings, Run button, and live diagnostics. Replace prompts that describe only `.m` with wording that covers all supported Moulang sources. The manifest will omit `.M`. VS Code's own suffix matching is case-insensitive, so it can still open an uppercase spelling in the language mode through the lowercase entry. The editor language mode and compiler import rules are independent. [VS Code language contributions](https://code.visualstudio.com/docs/languages/identifiers), [VS Code association implementation](https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/services/languagesAssociations.ts).

The legacy `.m` suffix overlaps Objective-C and MATLAB. Microsoft Project also uses `.mpp`. The plan adds VS Code language associations. In mixed workspaces, path-scoped `files.associations` settings can select a language for a particular directory. [VS Code Objective-C manifest](https://github.com/microsoft/vscode/blob/main/extensions/objective-c/package.json), [Linguist registry](https://github.com/github-linguist/linguist/blob/main/lib/linguist/languages.yml), [Microsoft Project file formats](https://support.microsoft.com/en-us/project/file-formats-supported-by-project-desktop), [Workspace language associations](https://code.visualstudio.com/docs/languages/overview#_add-a-file-extension-to-a-language).

## CTest and GitHub Actions

- Add a parameterized CTest entry covering `check`, `check --stdin --library`, `run`, default `build` output, and `build -o` for all seven suffixes. Use mixed-suffix source graphs, spaces and Unicode paths, missing and cyclic imports, and ML3028 cases such as `.M` and `.m+`. Retain tests for legacy programs and standard imports.
- Verify the exact seven manifest entries. Exercise automatic language mode, execution, snippets, and live diagnostics for the supported suffixes in the isolated VS Code source and installed VSIX suites. Treat an uppercase file recognized by the editor as case-insensitive matching.
- The current Windows workflow triggers on pushes to `main`, matching tags, pull requests, and manual dispatch. Its existing Release CTest and isolated VS Code steps can run the added checks. Run Debug CTest locally as part of Stage 12 verification. The current workflow has no suffix-based path filters. If a later workflow adds filters, escape literal plus characters in patterns such as `'**/*.m\+\+'` and `'**/*.mc\+'`. [GitHub Actions filter patterns](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#filter-pattern-cheat-sheet).
- The ZIP includes both `examples/hello.mcx` and `examples/hello.m` in its integrity manifest. The installed-package smoke test runs both. Artifact names and existing published packages are preserved.

GitHub Linguist has no Moulang entry in the registry checked for this review. The selected suffixes do not register one. A `linguist-language` override can select a known language and a path-scoped `-linguist-detectable` override can omit misclassified examples from statistics. Moulang highlighting on GitHub depends on a separate Linguist submission and acceptance. [Registry](https://github.com/github-linguist/linguist/blob/main/lib/linguist/languages.yml), [Linguist overrides](https://github.com/github-linguist/linguist/blob/main/docs/overrides.md), [Linguist contribution rules](https://github.com/github-linguist/linguist/blob/main/CONTRIBUTING.md#adding-a-language).

## Migration

Use `.mcx` for new files. Existing `.m` projects continue to work without deprecation warnings. An optional migration renames selected sources, updates their resolved import references and any tasks or scripts that name those files, then verifies the result with `check`, `run`, and `build`. A future read-only helper can preview those edits and report conflicting destination names. Applying renames remains a user action.

## Verification and changed files

Release passed 25 CTest entries and Debug passed 22. The extension-specific test covered all seven CLI entry types, mixed and duplicate imports, lowercase spelling rules, default and explicit executable paths, and unsaved library checks. VS Code unit tests and isolated extension source and installed VSIX tests passed. Both extension suites exercised language mode, snippet completion, execution, and live diagnostics for all seven extensions.

The local ZIP passed SHA-256 verification. Its extracted verifier checked the file manifest and required notices and ran both Hello World examples with development-tool discovery restricted. These checks ran on the local Windows x64 host. A hosted workflow run and a clean Windows installation remain separate verification.

- Compiler source suffixes and help in `include/moulang/source.hpp`, `src/source/program.cpp`, and `src/cli/main.cpp`.
- Extension associations and prompt in `vscode-extension/package.json` and `vscode-extension/src/extension.js`.
- Hello World, CTest, relocation, and extension tests in `examples/hello.mcx`, `CMakeLists.txt`, `tests/integration/source-extensions.ps1`, `tests/integration/relocation.ps1`, and `tests/vscode/suite.js`.
- Packaging and CI in `tools/Package-Windows.ps1`, `tools/Verify-Install.ps1`, and `.github/workflows/windows.yml`.
- Usage and status in `README.md`, `CHANGELOG.md`, `docs/README.md`, `docs/INSTALL.md`, `docs/RELEASE.md`, `docs/USER_GUIDE.md`, `docs/LANGUAGE_SPEC.md`, `docs/IMPLEMENTATION_STATUS.md`, and `vscode-extension/README.md`.
