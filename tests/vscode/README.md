# VS Code extension tests

Use Node.js 22 or later and the Windows x64 compiler bundle built from this checkout. Install dependencies with `npm ci --ignore-scripts --no-audit --no-fund` in vscode-extension. The development dependency versions and transitive packages are recorded in package-lock.json.

From the repository root:

```powershell
$env:MOULANG_TEST_COMPILER = (Resolve-Path 'build/windows-release/bin/moulang.exe').Path
Push-Location vscode-extension
npm run test:unit
npm test
Pop-Location
```

The unit tests exercise absolute, relative, and PATH lookup, spaces, missing files, and invalid settings. A simulated VS Code API loads the actual extension entry point, invokes its registered run command, and verifies both selecting and dismissing the missing-compiler settings action. The grammar tests use the real TextMate registry and Oniguruma WASM engine to verify supported escapes, quote/backslash boundaries, newline termination, and invalid-escape scopes. Semantic Unicode ranges and decoded UTF-8 validity are compiler checks.

The host runner uses @vscode/test-electron to download the pinned VS Code 1.144.0 archive into build/vscode-download. It never selects the user's installed Code executable. Each run uses fresh user-data, extensions, workspace, and temporary directories under build/vscode-runs, with explicit --user-data-dir and --extensions-dir arguments. Local Code extensions, profiles, and installer update locks are not used. The first run requires access to Microsoft's download service.

The host suite first loads the source extension. It then packages a VSIX, installs it into another isolated extensions directory, and tests that installed payload through an Extension Development Host. It verifies the exact extension ID and display name, activation, command registration, the play-button manifest conditions, current-file process arguments and working directory after switching editors, completion/snippets, compiler diagnostics on a Unicode path, and diagnostics for unsaved edits.

The play button's menu declaration is tested; an actual mouse click and visual rendering remain manual UI checks. VS Code 1.85, the minimum declared engine version, has not been tested by this pinned-host suite. Retained profiles and logs under build are ignored by Git. No Marketplace publishing or user-profile installation occurs.
