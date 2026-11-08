# Install Moulang on Windows 10/11 x64

1. Download the [0.2.0 ZIP](https://github.com/KaichuTheFox/Moulang/releases/download/v0.2.0/moulang-windows-x64.zip) and [SHA-256 file](https://github.com/KaichuTheFox/Moulang/releases/download/v0.2.0/moulang-windows-x64.zip.sha256). Verify the ZIP with Get-FileHash -Algorithm SHA256, then extract it to a folder you control, such as C:\Moulang. Keep the whole folder, including bin/toolchain and its licenses. The expected version is Moulang 0.2.0.
2. Add the extracted bin folder to PATH in your Windows user environment settings and reopen your terminal. Alternatively, use the full path to bin/moulang.exe. Run moulang --version and moulang run examples/hello.m from the extracted folder.
3. In VS Code, run Extensions: Install from VSIX and select the included moulang-0.1.5.vsix. Set moulang.executablePath to the full compiler path, such as C:\Moulang\bin\moulang.exe, without quotes. Open your source folder in VS Code, create and save a .m file inside that folder, then click the play button. Your source folder may differ from the compiler installation folder.

Run Verify-Install.ps1 from PowerShell to check package hashes and compile Hello World with restricted tool discovery. The script leaves its temporary test output for inspection. A successful test on a development PC does not establish clean-OS compatibility.

Generated programs are single executables that use Windows system DLLs. Preserve the applicable runtime notices from bin/toolchain/licenses when distributing them. This kit targets Windows 10/11 x64 with the OS-provided UCRT.
