# Install Moulang on Windows 10/11 x64

1. Obtain the compiler ZIP and its matching SHA-256 file. The current source bundle contains compiler 0.4.0 and extension 0.2.1. The published 0.3.0 preview is available as a [ZIP](https://github.com/KaichuTheFox/Moulang/releases/download/v0.3.0/moulang-windows-x64.zip) and [SHA-256 file](https://github.com/KaichuTheFox/Moulang/releases/download/v0.3.0/moulang-windows-x64.zip.sha256). Verify the ZIP with `Get-FileHash -Algorithm SHA256`, then extract the whole folder, including `bin/toolchain` and its licenses.
2. Add the extracted `bin` folder to PATH in your Windows user environment settings and reopen your terminal. Alternatively, use the full path to `bin/moulang.exe`. Run `moulang --version` and verify the version against `package-manifest.json`. From the extracted folder, run `moulang run examples/hello.mcx` with 0.4.0 or `moulang run examples/hello.m` with 0.3.0.
3. In VS Code, run **Extensions: Install from VSIX** and select the included VSIX. Set `moulang.executablePath` to the full compiler path, such as `C:\Moulang\bin\moulang.exe`, without quotes. Open your source folder in VS Code, save a Moulang source file inside that folder, then click the play button. Your source folder may differ from the compiler installation folder.

Run Verify-Install.ps1 from PowerShell to check package hashes and compile Hello World with restricted tool discovery. The script leaves its temporary test output for inspection. A successful test on a development PC does not establish clean-OS compatibility.

Generated programs are single executables that use Windows system DLLs. Preserve the applicable runtime notices from bin/toolchain/licenses when distributing them. This kit targets Windows 10/11 x64 with the OS-provided UCRT.
