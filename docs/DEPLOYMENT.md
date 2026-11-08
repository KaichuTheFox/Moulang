# Windows deployment

## Development compiler

Run cmake --install build/windows-release --prefix build/windows-install after using the build wrapper in the README. The resulting bin directory contains moulang.exe, moulang_runtime.lib, and the toolchain directory. Add it to PATH to use the compiler from CMD or PowerShell. Keep the complete directory together when moving a local installation.

The compiler locates toolchain/bin/ld.lld.exe and toolchain/lib relative to its executable. The runtime is separately compiled using LLVM-MinGW 20260616 with LLVM 22.1.8. The compiler itself is built with MSVC and LLVM 22.1.8. Their C++ objects are not linked together. Generated COFF objects target x86_64-w64-windows-gnu and communicate with the runtime using the Windows x64 C ABI.

The approved static kit targets Windows 10/11 x64 with the OS-supplied UCRT. It includes selected LLVM-MinGW static archives and startup objects, not standalone Microsoft SDK/MSVC development libraries. Preserve the complete notices under toolchain/licenses with the package and the applicable runtime notices when distributing generated programs. Use a fresh build/install directory to avoid obsolete files from earlier private kits.

## Generated executables

Moulang links its runtime and C++ support statically into a single generated Windows x64 executable. The static-runtime regression test checks that the imports are permitted Windows system DLLs, with no separately distributed Moulang, libc++, or compiler runtime DLL.

## VS Code extension

The VSIX installs separately from the native compiler. See the [README](../README.md#visual-studio-code) for packaging and setup. The package identity is `KaichuTheFox.moulang`. Earlier packages can be retained as release archives.

Release packaging and clean-Windows validation remain separate tasks. Public packaging must use a fresh staging directory and the reviewed inventory.
