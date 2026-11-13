# Next Moulang preview release preparation

The selected compiler version is 0.4.0 and the bundled VS Code extension version is 0.2.1. The extension uses the `KaichuTheFox.moulang` identity. The [0.4.0 preview notes](../RELEASE_NOTES_0.4.0.md) describe the features, integer narrowing migration, and installation.

## Version and publication sequence

1. The selected versions are in `CMakeLists.txt`, the extension manifest, and the extension lockfile. The changelog labels the preview as unreleased. Public download links point to the published 0.3.0 preview until the new assets exist.
2. Review the source diff before pushing. A regular `main` push or a pull request runs the build and installation jobs without a version tag. Transfer the resulting ZIP and checksum to clean Windows 10 and Windows 11 x64 environments and follow the [clean Windows steps](../RELEASE.md#additional-clean-windows-check).
3. After validation and publication approval, push `v0.4.0` to trigger GitHub Release publication. Verify the published ZIP and checksum, then update public download links. VS Code Marketplace publication is a separate action using the same extension identity.

## Versioned build verification

The compiler 0.4.0 Release and Debug builds passed 27 of 27 and 24 of 24 CTest entries respectively. Nine extension unit tests and one repository-link test passed. The isolated VS Code 1.144.0 source and installed VSIX suites passed with extension 0.2.0.

The local ZIP includes the 0.4.0 compiler, the native link kit, and `moulang-0.2.0.vsix`. A fresh extraction verified the ZIP checksum, all 29 manifest entries, the required third-party notices, and both Hello World examples with restricted development-tool discovery. The compiler and runtime hashes matched the tested Release build. The extracted compiler also passed checks for UTF-8 indexing with block formatting, the Deep warning, and the runtime list-mutation guard.

The local bundle was repackaged to include the `KaichuTheFox` and `Team.HaZos` copyright notices in the project and extension MIT license files. The ZIP is 47,813,663 bytes and its SHA-256 is `c4cd1aac96af11d215478df525832e39aa78015b3120e3c5cc6e32e128e66dc2`. A new extraction passed checksum, manifest, notice, and Hello World verification. Both packaged MIT license texts match their source files.

The updated local bundle pairs compiler 0.4.0 with extension 0.2.1. Its SHA-256 is `9dd22f4a3ce80c144be75c3fb19f0a5768ef7f49c50e7fd340c2d082f3c16f36`. The ZIP and its per-file manifest passed verification, and the extracted compiler ran both Hello World examples. The source extension and installed 0.2.1 VSIX also passed the isolated editor suites.

The hosted GitHub Actions run and clean Windows 10 and Windows 11 validation remain pending. The local bundle is available for those tests.

## Preflight before the version update

A fresh source copy with cached pinned toolchains built the compiler and passed Release CTest 27 of 27. A fresh npm install from the lockfile passed nine extension unit tests and one repository-link test. The isolated VS Code 1.144.0 source and installed VSIX suites both passed. The editor logged background tokenization and terminal-host warnings, so this run does not verify the visual rendering of syntax highlighting. The workflow YAML parsed and its build, installation, and tag release dependencies matched the expected order.

The fresh build produced a local test ZIP with compiler 0.3.0 and extension 0.1.6. The ZIP checksum, 29 file hashes, required notices, and both Hello World examples passed on the development host with restricted tool discovery. Its SHA-256 is `493f51cb4c9818ba7d57b3f48cfdbdc64da03d17a80fa6457f327e577906a1d6`. The hosted workflow for the pending source and clean Windows 10 and 11 verification remain open.
