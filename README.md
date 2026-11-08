# Moulang

<img src="vscode-extension/images/moulang-icon.png" alt="Moulang logo" width="128">

Moulang is a statically typed programming language from the HAZOS development team. The project adopted the Moulang name while keeping its `.m` source format. The compiler is written in C++20 and uses LLVM to produce native Windows x64 executables. The project aims to make native compilation and source-level diagnostics easy to use while keeping the language small.

## Install without building

**Currently Windows x64 only.** Linux portability is a [design proposal](docs/PLATFORM_DESIGN.md); no Linux implementation has been validated.

Moulang 0.2.0 is the Windows x64 preview release. It includes string escapes, signed 64-bit long and ordinary function overloads. See the [release notes](docs/RELEASE_NOTES_0.2.0.md) for compatibility changes and remaining limitations.

1. Download the [compiler ZIP](https://github.com/KaichuTheFox/Moulang/releases/download/v0.2.0/moulang-windows-x64.zip) and [SHA-256 file](https://github.com/KaichuTheFox/Moulang/releases/download/v0.2.0/moulang-windows-x64.zip.sha256) from [Release v0.2.0](https://github.com/KaichuTheFox/Moulang/releases/tag/v0.2.0). Compare `Get-FileHash .\moulang-windows-x64.zip -Algorithm SHA256` with the checksum file, then extract the whole ZIP, for example to `C:\Moulang`.
2. Add its `bin` folder to your Windows user `PATH`, reopen the terminal, and run `moulang --version`. Keep `bin/toolchain` and its notices alongside the compiler.
3. In VS Code, run **Extensions: Install from VSIX** and select the included VSIX. Set `moulang.executablePath` to `C:\Moulang\bin\moulang.exe` if that is your extraction location. Open your source project folder, create and save a `.m` file inside it, then click the play button. Compiler and source folders may be different.

The package targets Windows 10/11 x64 and uses Windows system DLLs. Run its included `Verify-Install.ps1` to check hashes and compile Hello World. See the [release and clean-Windows validation guide](docs/RELEASE.md).

## Hello, world

Save this as `hello.m`.

```moulang
main() {
    print("Hello Moulang!")
}
```

## Build from source

Moulang currently targets Windows 10/11 x64. Source builds need Visual Studio with the **Desktop development with C++** workload, the Windows SDK, CMake 3.25 or later, Ninja, and the pinned LLVM 22.1.8 development and LLVM-MinGW 20260616 UCRT packages. Run `powershell -ExecutionPolicy Bypass -File tools/Install-Toolchains.ps1` to download both archives and verify their pinned SHA-256 digests.

From an x64 **Developer PowerShell for VS**, open the repository root. Adjust the LLVM path to match the location where you extracted it.

The reproducible build wrapper initializes the installed MSVC tools, detects their localized include prefix, configures CMake, builds, and runs CTest:

```powershell
./tools/Build-Windows.ps1 -BuildDirectory build/windows-release
```

For manual CMake configuration:

```powershell
$llvm = (Resolve-Path '.toolchain/clang+llvm-22.1.8-x86_64-pc-windows-msvc').Path
cmake -S . -B build/static-toolchain/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release "-DLLVM_DIR=$llvm/lib/cmake/llvm" "-DCMAKE_LINKER=$llvm/bin/lld-link.exe"
cmake --build build/static-toolchain/release
ctest --test-dir build/static-toolchain/release --output-on-failure
cmake --install build/static-toolchain/release --prefix build/static-toolchain/install
```

The compiler bundle is in the build directory's `bin` folder. Keep the entire `bin` directory, including `moulang.exe`, `moulang_runtime.lib`, `toolchain`, and its license notices, together. Add `bin` to `PATH` to use the commands below. Use a fresh build directory such as `build/static-toolchain/release` when switching from the previous private kit. This keeps obsolete library copies out of the package. See the [privacy and distribution review](docs/PRIVACY.md).

## Run, build, and check

```powershell
moulang --version
moulang --help
moulang run examples/hello.m
moulang build examples/hello.m -o build/moulang/hello.exe
.\build\moulang\hello.exe
moulang check examples/objects.m
```

- `run` compiles and runs a temporary executable. It forwards the program's exit code.
- `build` writes an executable. Without `-o`, it writes the executable to the current directory using the source file's base name. Use `-O0`, `-O2`, or `-O3` to set optimization.
- `check` parses and type checks source. It accepts `--stdin` for unsaved text and `--library` for source that does not need a `main` entry point.

## Language example

```moulang
int doubled(int value) {
    return value * 2
}

main() {
    int numbers[] = [10, 20, 30]
    foreach (item in numbers) {
        print(doubled(item))
    }
}
```

Moulang also supports value structs, nullable values, and relative imports. Classes, fixed integer arrays, and local view checks have limited implementations. See the [language specification](docs/LANGUAGE_SPEC.md) and [implementation status](docs/IMPLEMENTATION_STATUS.md) for details.

Current source builds support string escapes, including UTF-8 Unicode scalar escapes and embedded NUL:

```moulang
main() {
    print("first\nsecond\t\"quoted\"")
    print("\xC3\xA9" == "\u{E9}")
    print("A\0B" == "A\x00B")
}
```

Use `\\` for a literal backslash, or `/` in file paths. Unknown escapes and invalid decoded UTF-8 are compile errors. These rules are included in compiler 0.2.0; older development archives may predate them. See the [string rules](docs/LANGUAGE_SPEC.md#string-escapes).

## Visual Studio Code

Current source builds use signed 64-bit `long`. Literals beyond the int range infer long; `int(longValue)` checks its range at runtime. Convert long values explicitly before mixing them with real values, for example `double(value) + 0.5`. Ordinary functions may overload parameter signatures within one file; exact argument types take precedence. See the [approved integer rules](docs/LONG_DESIGN.md) and [language specification](docs/LANGUAGE_SPEC.md).

Package the extension with its pinned development dependencies:

```powershell
Push-Location vscode-extension
npm ci --ignore-scripts --no-audit --no-fund
npm run package
Pop-Location
```

In VS Code, select **Extensions > Install from VSIX** and open that package. Open the folder containing your `.m` file. If the compiler is outside `PATH`, set **Moulang: Executable Path** (`moulang.executablePath`) to the full path to `moulang.exe`. If you configured an earlier extension, copy its compiler path into this setting and disable the earlier extension to avoid duplicate file associations.

Open a saved `.m` file and click the play button in the upper right of the editor. The **Moulang: Run Current File** command in the Command Palette does the same thing. The extension saves edits before running. Output appears in the terminal and compiler errors appear in **Problems**. **Moulang: Live Diagnostics** checks unsaved changes in a trusted workspace.

The display name is **Moulang for VS Code** and the identity is `KaichuTheFox.moulang`. The bundled extension 0.1.5 is tested with compiler 0.2.0; existing extension 0.1.4 can also invoke this compiler by updating its executable path. Extension 0.1.5 is included as a VSIX; publishing it to the Marketplace is a separate step. When the compiler cannot be found, **Open Settings** opens `moulang.executablePath` directly. See the [extension testing guide](tests/vscode/README.md).

To load the extension from source, run this from the repository root:

```powershell
code --extensionDevelopmentPath="$((Resolve-Path vscode-extension).Path)" .
```

## Troubleshooting

See the [diagnostic improvement proposals](docs/DIAGNOSTIC_PROPOSALS.md) for ten current examples and suggested source underlines/hints. These presentation changes are not yet implemented.

- **moulang is not recognized.** Add the compiler directory to PATH and reopen PowerShell, or use the full executable path.
- **CMake cannot find LLVM.** Point LLVM_DIR to the extracted package's lib/cmake/llvm directory. Use an x64 Developer PowerShell and point CMAKE_LINKER to lld-link.exe from the same archive.
- **Ninja misses header changes with localized MSVC.** Set MOULANG_MSVC_INCLUDE_PREFIX to the literal prefix printed before file paths by cl /showIncludes. Then rebuild with cmake --build build/static-toolchain/release --clean-first.
- **A Debug build reports MSVC error C1902.** Configure a fresh Debug build directory with `-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`. For Debug builds with CMake older than 4.0, also pass `"-DCMAKE_CXX_FLAGS_DEBUG=/Od /Ob0"` to keep MSVC runtime check instrumentation out of the standalone runtime.
- **The compiler reports a missing bundled toolchain file.** Copy the entire local `bin` directory rather than only the executable. Rebuild or reinstall the complete local bundle if a file is missing. If the build machine's development tools move, reconfigure CMake before rebuilding.

## Project information

Heap-backed slices, class objects and file-read string storage currently remain allocated until process exit. See the [memory model comparison and recommendation](docs/ROADMAP.md#stage-9-memory-model-proposal). Automatic reclamation and Godot integration are future work, not implemented features.

Moulang is developed by HAZOS and distributed under the [MIT License](LICENSE).

## Distribution privacy review

Release builds omit compiler debug information and map compiler source/object paths when supported. The compiler resolves its approved LLVM-MinGW static kit relative to its own directory. It packages selected open-source static libraries and complete notices rather than standalone Microsoft development libraries. Clean-Windows and release-package validation remain separate checks. Read the [manual backup, email cleanup, and distribution review](docs/PRIVACY.md) before a public binary release or history rewrite.

- [Compiler architecture](docs/COMPILER_ARCHITECTURE.md)
- [User guide](docs/USER_GUIDE.md)
- [Changelog](CHANGELOG.md)
- [Development roadmap](docs/ROADMAP.md)
- [Windows deployment](docs/DEPLOYMENT.md)
- [Release workflow and clean-Windows checks](docs/RELEASE.md)
- [Repository rename checklist](docs/REPOSITORY_RENAME.md)
- [Local work report and remaining approvals](docs/WORK_REPORT.md)
