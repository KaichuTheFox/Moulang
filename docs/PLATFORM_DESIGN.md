# Platform isolation proposal (stage 7; no implementation)

The implemented target is Windows x64 only. Production linkage now uses GNU-flavor PE ld.lld; lld-link still builds the MSVC host compiler.

## Dependency inventory

- src/codegen/llvm/object.cpp: fixed x86_64-w64-windows-gnu triple, X86 target machine/data layout and native object emission. The triple selects COFF rather than ELF.
- src/driver/windows.cpp: GetModuleFileNameW, CreateProcessW, wait/exit APIs, Windows argument quoting, MoveFileExW, .exe/.obj/.lib names, PE link arguments, startup objects and static libraries.
- cmake/toolchain.hpp.in: relative ld.lld.exe and import/static library inventory. CMakeLists.txt: WIN32/MSVC/x64 restriction, DIA SDK workaround, MSVC flags/path mapping, LLVM-MinGW runtime compilation and Windows test launch.
- runtime/src/runtime.cpp: console UTF-8/UTF-16 conversion, handles, HeapAlloc/HeapFree, ExitProcess, CreateFileW, ReadFile/WriteFile, MAX_PATH buffers, temporary files, FlushFileBuffers and MoveFileExW.
- runtime/src/real.cpp: Windows header inclusion; numeric algorithms can be separated into portable runtime sources later.
- src/source/source.cpp: MultiByteToWideChar UTF-8 validation. src/cli/main.cpp: wmain, Windows console output and _setmode/_fileno binary stdin.
- tools/Build-Windows.ps1, Install-Toolchains.ps1, packaging/verifier scripts: VS/MSVC discovery, Windows archives, System32 paths and Windows package inventory. tests/integration/static-runtime.ps1 checks PE imports. CI currently uses Windows runners.
- vscode-extension/src/compiler.js: platform-aware PATH delimiter and Windows .exe fallback. Tasks pass structured process arguments through node:path helpers.

## Backslash inspection

Path joins generally use std::filesystem or CMake path helpers. Literal backslashes in src/driver/windows.cpp implement Windows argument quoting and must remain there. Lexer backslashes implement language escapes, not file paths. CMake regexes accept both separators. PowerShell commands intentionally use Windows paths. Extension joins use node:path. Runtime MAX_PATH limits remain; this inventory does not claim unrestricted long-path support.

## Proposed boundaries

TargetBackend would provide targetTriple(), objectFormat(), runtimeInputs(), linkerInvocation(), executableSuffix() and publishExecutable(). WindowsTarget would retain current PE behavior. A future LinuxTarget would select an ELF triple, ELF startup objects, ld.lld arguments and a verified Linux sysroot/runtime. Pass path objects and argument vectors, never shell command fragments.

Keep HostServices separate for executable location, process launch/wait, console output, UTF-8 validation and atomic publication. Host describes the machine running the compiler; target describes generated code. PlatformRuntime must separately wrap allocation/freeing, byte I/O, files and process termination. A compiler-only interface cannot port the existing Windows runtime. Preserve the explicit C ABI and avoid passing C++ objects across toolchains.

First wrap current Windows behavior, then route configuration through target descriptions, and only afterward implement a Linux runtime/backend. Future tests should inspect argument vectors, object headers, runtime ABI, Unicode/space paths, relocation and actual output on both systems. Open choices include Linux libc/distribution baseline, bundled versus system tools, cross-compilation scope and CLI target selection.

This stage contains no interface implementation, ELF backend, Linux runtime or Linux CI. Validation is source inventory and documentation-link checking, not a Linux build.
