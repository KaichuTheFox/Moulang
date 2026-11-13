# Windows build and release verification

The Windows workflow runs on main pushes, pull requests, manual dispatch, and v-prefixed tags. Its official actions are pinned to reviewed commit IDs.

1. The build job runs on windows-latest, verifies and extracts the exact LLVM 22.1.8 and LLVM-MinGW 20260616 archives, configures a new CMake/Ninja build, compiles the compiler and separate static runtime, and runs CTest.
2. It runs extension unit tests and the downloaded, isolated VS Code host suites against source and VSIX payloads. It then installs into a fresh staging directory, packages the extension using the npm lockfile, includes the runtime/import libraries and required notices, writes per-file SHA-256 hashes, and produces a ZIP and ZIP checksum.
3. A separate installation job downloads only that artifact, checks its ZIP checksum, extracts it, and runs the packaged verifier. That verifier checks every listed file, rejects unlisted files and path traversal, clears development-tool discovery variables, limits PATH to Windows System32, and runs both the `.mcx` and `.m` Hello World examples.
4. A tag push matching the compiler's CMake version publishes the same verified ZIP and checksum as a GitHub preview Release after both jobs pass. The release job alone has contents write permission and uses the versioned release-notes document. Existing tags/assets are never replaced. This workflow does not publish to the extension Marketplace.

The hosted Windows runner already contains development tools. The installation job does not check out source, install LLVM, or configure a compiler, but its restricted discovery test is not a clean-OS proof. The verifier reports whether a Visual Studio installer is present. The archive's own compiler/link kit is used explicitly.

The static-runtime CTest runs `llvm-readobj --coff-imports` on the compiler and on programs built from the hello, numeric, object, text, and list examples. It checks separate allowlists for the compiler and generated programs, including the specific Windows UCRT API sets observed in the supported build. Review new imports when the toolchain changes. The installation verifier checks every file hash, rejects duplicate paths, and requires the third-party notices in the extracted archive.

Packaging scans the compiler, runtime, shipped examples, project text files, and unpacked VSIX entries for developer home paths and the current source root in UTF-8 and aligned or unaligned UTF-16LE. It scans the pinned vendor linker separately. The upstream static libraries and license files retain their original attribution and are covered by the pinned toolchain archive and the per-file manifest.

## Run the workflow bodies locally

```powershell
./tools/Install-Toolchains.ps1
./tools/Build-Windows.ps1 -BuildDirectory build/windows-release
Push-Location vscode-extension
npm ci --ignore-scripts --no-audit --no-fund
$env:MOULANG_TEST_COMPILER = (Resolve-Path '../build/windows-release/bin/moulang.exe').Path
npm run test:unit
npm test
Pop-Location
./tools/Package-Windows.ps1 -BuildDirectory build/windows-release -OutputDirectory build/release
Expand-Archive -LiteralPath build/release/moulang-windows-x64.zip -DestinationPath build/install-check
./build/install-check/Verify-Install.ps1 -Root (Resolve-Path build/install-check).Path
```

Use a new output and installation directory for each test. Existing package files are preserved. The package manifest is an integrity check, not a digital signature. The source toolchain hashes are pinned separately in tools/toolchains.json.

The local workflow-body test configured and built a new directory, passed CTest, created a VSIX and ZIP, verified the ZIP checksum, extracted it, checked the file manifest, and ran Hello World with restricted discovery. Workflow YAML parsing also succeeded. The local sandbox denied linking an executable in its default temporary directory on the first attempt; using a writable temporary directory under the test workspace passed. That sandbox result does not establish behavior on every clean Windows installation. Hosted Actions execution, tag permissions, and Release upload have not been exercised by these local tests.

## Additional clean Windows check

Test on clean Windows 10 and Windows 11 x64 virtual machines with no Visual Studio, LLVM, MinGW, or prior Moulang installation. Transfer only the ZIP and its checksum. Compare the ZIP SHA-256, extract it, and run Verify-Install.ps1 from PowerShell. Confirm its host report and Hello World result. Keep Windows' own UCRT and system components installed.

On editions supporting Windows Sandbox, place the ZIP and checksum under a folder such as C:\MoulangReleaseCheck. A manually created sandbox configuration can map that folder read-only and disable networking:

```xml
<Configuration>
  <Networking>Disable</Networking>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>C:\MoulangReleaseCheck</HostFolder>
      <SandboxFolder>C:\ReleaseInput</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
  </MappedFolders>
</Configuration>
```

Inside the sandbox, compare the checksum, expand the ZIP to C:\Moulang, and run C:\Moulang\Verify-Install.ps1 with that Root. A sandbox uses the host's Windows generation; also test the other supported Windows generation separately. No clean-VM or sandbox result has been claimed from the local development-host tests.

## Manual release trigger

After local gates pass and main-branch CI succeeds, create and push a version tag matching CMakeLists.txt. For compiler 0.2.0 the commands are:

```powershell
git tag v0.2.0
git push origin v0.2.0
```

Tag publication must be explicitly authorized by the owner. The owner authorized v0.2.0 publication. Check [Release v0.2.0](https://github.com/KaichuTheFox/Moulang/releases/tag/v0.2.0) for its actual assets. The preview label remains while separate clean-Windows checks are pending. Do not overwrite an existing tag or Release on a retry.
