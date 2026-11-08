# Moulang 0.2.0 preview

Windows 10/11 x64 compiler from HAZOS. This is a preview; clean Windows installations without development tools still require additional verification. Hosted Windows runners contain development tools even when discovery is restricted during installation tests.

## Downloads and installation

- [Compiler ZIP](https://github.com/KaichuTheFox/Moulang/releases/download/v0.2.0/moulang-windows-x64.zip)
- [SHA-256 file](https://github.com/KaichuTheFox/Moulang/releases/download/v0.2.0/moulang-windows-x64.zip.sha256)

Compare the ZIP's Get-FileHash SHA256 result with the checksum file before extracting it. Keep the complete bin folder and third-party notices. Add bin to PATH or configure moulang.executablePath to the full path of bin/moulang.exe. Install the bundled moulang-0.1.5.vsix using Extensions: Install from VSIX. The extension does not download a compiler automatically.

Run Verify-Install.ps1 from the extracted root to validate file hashes and compile Hello World. moulang --version must print Moulang 0.2.0. Open the folder containing your saved .m source in VS Code before using the play button. See the [installation guide](https://github.com/KaichuTheFox/Moulang/blob/v0.2.0/docs/INSTALL.md).

## Changes and compatibility

- UTF-8 string escapes: control characters, quotes, backslash, NUL, hexadecimal bytes and Unicode scalar escapes. Unknown escapes are errors; use a doubled backslash for literal backslashes in existing source.
- long is now signed 64-bit. Large literals infer long, int-to-long widens implicitly, and explicit long-to-int conversion checks the signed 32-bit range at runtime.
- Mixed long/real operations require an explicit conversion. Integer arithmetic wraps at its type width; integer division by zero and invalid shift counts are runtime errors.
- Ordinary functions can overload parameter signatures within one declaring file. Ambiguous calls and conflicting public names across modules are errors. Methods and constructors do not overload.
- The relocatable static compiler kit includes the runtime, GNU PE linker, selected libraries and complete third-party license texts. LLVM 22.1.8 and LLVM-MinGW 20260616 archive hashes are pinned.

## Validation and limitations

Release CTest, extension unit/grammar tests, isolated downloaded VS Code source/VSIX suites and package checksum/install verification gate publication. The main-branch workflow and tagged workflow build from committed source; the tagged build produces the actual release bytes and checksum.

Heap slices, class objects and file-read strings still lack general reclamation. Linux/ELF, richer source underlines, automatic memory management and Godot integration remain future work. Do not treat a hosted installation check as a clean-OS guarantee. Existing 0.1.x archives and Git history are preserved; no tag is replaced.
