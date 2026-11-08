# Install Moulang on Windows 10/11 x64

1. Download the release ZIP and its SHA-256 file. Verify the ZIP with Get-FileHash -Algorithm SHA256, then extract it to a folder you control. Keep the whole folder, including bin/toolchain and its licenses.
2. Add the extracted bin folder to PATH in your Windows user environment settings and reopen your terminal. Alternatively, use the full path to bin/moulang.exe. Run moulang --version and moulang run examples/hello.m from the extracted folder.
3. In VS Code, run Extensions: Install from VSIX and select the included .vsix. Set moulang.executablePath to the full compiler path if it is outside PATH. Open a saved .m file and click the editor's play button.

Run Verify-Install.ps1 from PowerShell to check package hashes and compile Hello World with restricted tool discovery. The script leaves its temporary test output for inspection. A successful test on a development PC does not establish clean-OS compatibility.

Generated programs are single executables that use Windows system DLLs. Preserve the applicable runtime notices from bin/toolchain/licenses when distributing them. This kit targets Windows 10/11 x64 with the OS-provided UCRT.
