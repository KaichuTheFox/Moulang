# Third-party distribution inventory

The public link kit uses LLVM-MinGW 20260616 UCRT x86_64, built with LLVM 22.1.8. The archive URL and publisher-provided SHA-256 are pinned in `tools/toolchains.json`. The main compiler and static LLD linker use the official LLVM 22.1.8 Windows MSVC development archive, separately pinned there.

- LLVM, LLD, libc++, libc++abi/compiler runtime support, and libunwind use the applicable Apache-2.0 with LLVM exceptions and legacy notices preserved in `licenses`.
- MinGW-w64 import libraries and startup/runtime code carry ZPL-2.1 and component notices. The supplied MinGW COPYING and runtime notice are preserved in full. Notices that describe optional upstream components do not imply those components are shipped.
- The package includes only the selected x86_64 static archives, startup objects, LLD, and notices. It does not copy Microsoft SDK or MSVC standalone link libraries, MinGW GCC/binutils executables, or profiling code.
- UCRT imports resolve to Windows system components on Windows 10/11. The package does not include Windows system DLLs. Third-party runtime DLL imports must be rejected by release validation.

Moulang's generated programs statically include selected runtime components. Retain the applicable runtime notices when distributing those executables. The compiler itself is built with MSVC and links its own CRT; this does not authorize redistribution of Microsoft's standalone development libraries.

Upstream source references used for this pinned kit are LLVM `llvmorg-22.1.8`, LLVM-MinGW `20260616`, and the latter's MinGW-w64 revision `c28e9555bb8800c53449f42a465ad9a5676fce88`. License texts were copied without modification. Public copyright holder contact details inside upstream notices are intentionally retained attribution, not project developer private email addresses.

The package inventory and import checks must be regenerated and reviewed when these versions or the selected files change. Tests establish compatibility on the tested machines, not a general guarantee for every Windows build.
