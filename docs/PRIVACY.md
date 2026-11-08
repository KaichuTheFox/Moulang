# Stage 1 privacy review and manual history procedure

Status on 2026-11-07 is **approved static toolchain implemented; release validation in progress**. The owner selected historical email cleanup through the manual procedure and a compiler-relative bundled toolchain. History rewriting and remote replacement remain manual operations. The current working repository has not been rewritten.

## Findings and limits

- The local reachable history contains two commits. Commit `5ef29ba` has a non-noreply author and committer email. Commit `1adcccf` uses the GitHub noreply address for both. This is a local audit, not a new fetch of every remote ref.
- A rewrite changes commit IDs, including descendants of the changed commit, and may invalidate commit or tag signatures, links, pull requests, and collaborators' clones. Changing future Git configuration does not change existing commits.
- Rewriting reachable history does not erase copies in forks, clones, screenshots, notifications, search caches, or downloaded patches. GitHub may retain cached or pull request references. Contact GitHub Support for applicable cleanup. An already public email cannot be made secret again.
- Backups deliberately retain the original email. Store them privately outside the repository. Never attach a backup, mailmap, or unredacted history dump to an issue or release.

The owner chose to rewrite historical email metadata manually after backup and verification. Approval of this plan does not mean these destructive commands have already been run.

## Protect future commits

1. Open GitHub **Settings > Emails** at https://github.com/settings/emails.
2. Enable **Keep my email addresses private**.
3. Enable **Block command line pushes that expose my email**. GitHub describes this as checking the author email of the most recent commit during a push. It is not a full history audit.
4. Copy the noreply address displayed in those settings and use it locally.

```powershell
# Run from this project's working repository. Verify the address against GitHub.
$noreply = '293786688+KaichuTheFox@users.noreply.github.com'
git config --local user.email $noreply
git config --local --get user.email
```

An explicit `GIT_AUTHOR_EMAIL` or `GIT_COMMITTER_EMAIL` environment variable can override Git configuration. Review any such overrides locally without publishing their values.

## Back up before any rewrite

Close editors and pause pushes with collaborators. Start in the original working repository. Pick a new private location outside this repository. The copy can be large because it includes ignored toolchains and build outputs.

```powershell
$ErrorActionPreference = 'Stop'
$repoRoot = (git rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Start inside the project repository.' }
Set-Location -LiteralPath $repoRoot
$remoteUrl = (git remote get-url origin).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine the remote.' }

# Set a private backup location on an appropriate drive.
$backupRoot = [IO.Path]::GetFullPath('D:\Moulang-private-backup\before-email-rewrite')
if ($backupRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $backupRoot.StartsWith($repoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The backup must be outside the working repository.'
}
if (Test-Path -LiteralPath $backupRoot) { throw 'Use a new backup directory.' }
New-Item -ItemType Directory -Path $backupRoot | Out-Null

$bundle = Join-Path $backupRoot 'before.bundle'
git bundle create $bundle --all
if ($LASTEXITCODE -ne 0) { throw 'Bundle creation failed.' }
git bundle verify $bundle
if ($LASTEXITCODE -ne 0) { throw 'Bundle verification failed.' }

# Copy working files, ignored outputs, and .git. /E copies without deleting.
# /XJ avoids following junctions. Back up intentional junction targets separately.
$copyRoot = Join-Path $backupRoot 'working-copy'
robocopy $repoRoot $copyRoot /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /XJ
if ($LASTEXITCODE -ge 8) { throw 'Working-copy backup failed.' }

# Record the remote refs privately so later leases use the reviewed old IDs.
git ls-remote $remoteUrl | Set-Content -Encoding utf8 (Join-Path $backupRoot 'remote-refs.txt')
if ($LASTEXITCODE -ne 0) { throw 'Remote ref inventory failed.' }
```

A bundle contains commits and refs but not uncommitted or ignored working files. The separate copy preserves those files. A backup is only verified after checking both the bundle and the copy. Do not overwrite your working repository to test recovery.

```powershell
$restore = Join-Path $backupRoot 'restore-check'
git clone $bundle $restore
if ($LASTEXITCODE -ne 0) { throw 'Bundle recovery check failed.' }
git -C $restore fsck --full
if ($LASTEXITCODE -ne 0) { throw 'Recovered object check failed.' }
```

This recovery restores the old metadata locally. Restoring it to the public remote would publish that metadata again.

## Prepare an isolated rewrite after owner approval

Use a fresh mirror rather than the development repository. The commands below assume the backup variables remain in the same PowerShell session. The email mapping file contains private data and stays outside Git.

```powershell
$tools = Join-Path $backupRoot 'filter-tools'
python -m venv $tools
if ($LASTEXITCODE -ne 0) { throw 'Tool environment creation failed.' }
$scripts = Join-Path $tools 'Scripts'
& (Join-Path $scripts 'python.exe') -m pip install 'git-filter-repo>=2.47'
if ($LASTEXITCODE -ne 0) { throw 'Tool installation failed.' }
$env:PATH = $scripts + ';' + $env:PATH
git filter-repo --version
if ($LASTEXITCODE -ne 0) { throw 'git-filter-repo is unavailable.' }

$mirror = Join-Path $backupRoot 'rewrite.git'
git clone --mirror $remoteUrl $mirror
if ($LASTEXITCODE -ne 0) { throw 'Mirror clone failed.' }
$oldHead = (git -C $mirror rev-parse refs/heads/main).Trim()
$oldTree = (git -C $mirror rev-parse 'refs/heads/main^{tree}').Trim()
$oldCount = (git -C $mirror rev-list --count refs/heads/main).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Record the main ref before filtering.' }

# Enter only the email approved for replacement. Other contributors are untouched.
$oldEmail = Read-Host 'Old email to replace, kept only in this private backup'
if ($oldEmail -notmatch '^[^<>\s]+@[^<>\s]+$') { throw 'Invalid old email.' }
$noreply = '293786688+KaichuTheFox@users.noreply.github.com'
$mailmap = Join-Path $backupRoot 'email-map.private'
[IO.File]::WriteAllText($mailmap, "<$noreply> <$oldEmail>" + "`n", [Text.UTF8Encoding]::new($false))

git -C $mirror filter-repo --sensitive-data-removal --mailmap $mailmap
if ($LASTEXITCODE -ne 0) { throw 'Filtering failed. Review the isolated copy.' }
```

The email-only mailmap keeps display names while mapping matching author, committer, and tagger emails. It does not rewrite addresses embedded in file contents or commit messages. Review those separately. If the repository is not considered fresh, create another fresh mirror after reviewing the cause instead of bypassing the tool's safety check.

## Verify before changing the remote

```powershell
$rows = @(git -C $mirror log --all '--format=%ae%x09%ce')
if ($LASTEXITCODE -ne 0) { throw 'History verification failed.' }
$remaining = @($rows | Where-Object {
    @($_ -split "`t" | Where-Object { $_ -ieq $oldEmail }).Count -gt 0
})
$tagEmails = @(git -C $mirror for-each-ref '--format=%(taggeremail:trim)' refs/tags)
if ($LASTEXITCODE -ne 0) { throw 'Tag verification failed.' }
if ($remaining.Count -gt 0 -or @($tagEmails | Where-Object { $_ -ieq $oldEmail }).Count -gt 0) {
    throw 'An approved old email remains in commit or tag metadata.'
}
$newHead = (git -C $mirror rev-parse refs/heads/main).Trim()
$newTree = (git -C $mirror rev-parse 'refs/heads/main^{tree}').Trim()
$newCount = (git -C $mirror rev-list --count refs/heads/main).Trim()
if ($newTree -ne $oldTree -or $newCount -ne $oldCount) {
    throw 'Review unexpected source tree or main commit count changes.'
}
git -C $mirror fsck --full
if ($LASTEXITCODE -ne 0) { throw 'Rewritten repository verification failed.' }
```

Review the filter-repo commit map and changed refs. A metadata-only rewrite should preserve the main branch tree. Signed metadata may need re-signing. Pause until every affected branch and tag has an explicit update plan.

## Manual force push, only after a separate owner decision

The following step replaces remote history. Run it yourself only after backups, verification, collaborator coordination, and any required branch protection review. The explicit lease stops if another push has changed main. Do not simply refresh the lease to override someone else's work.

```powershell
# filter-repo may remove origin as a safety measure.
if (@(git -C $mirror remote) -notcontains 'origin') {
    git -C $mirror remote add origin $remoteUrl
    if ($LASTEXITCODE -ne 0) { throw 'Cannot restore the origin remote.' }
}
$lease = '--force-with-lease=refs/heads/main:' + $oldHead
git -C $mirror push $lease origin 'refs/heads/main:refs/heads/main'
if ($LASTEXITCODE -ne 0) { throw 'Push rejected. Stop and inspect remote changes or protection.' }
```

This example updates only main. It does not delete remote branches. If other branches or tags contain the old history, record their old ref IDs and review individual ref updates with explicit leases as well. Do not replace all remote refs blindly. GitHub pull request refs may require support assistance.

```powershell
$published = @(git ls-remote $remoteUrl refs/heads/main)
if ($LASTEXITCODE -ne 0 -or $published.Count -ne 1 -or
    ($published[0] -split '\s+')[0] -ne $newHead) { throw 'Remote main does not match the reviewed rewrite.' }
$verification = Join-Path $backupRoot 'verify-remote.git'
git clone --mirror $remoteUrl $verification
if ($LASTEXITCODE -ne 0) { throw 'Remote verification clone failed.' }
# Repeat the metadata check above with $mirror set to $verification.
# Review public commit pages and coordinate fresh clones with collaborators.
```

Keep old clones private. Do not merge or push their old branches into the rewritten history. Request support review of retained refs or cached views where applicable. History cleanup cannot revoke copies already obtained by others.

## Binary investigation

The generated `cmake/toolchain.hpp.in` embeds linker, Windows SDK, and MSVC static library paths as wide string literals. The driver reads these paths during linking. These literals survive Release optimization and are independent of PDB settings.

The previous local Release executable has no PE debug directory and no RSDS marker, but still contains the configured absolute paths. The previous runtime archive also contains compiler object-name and debug records. No `__FILE__` use was found in production project source. Headers and third-party code can still introduce it, so a separate regression fixture deliberately uses `__FILE__`.

Release and MinSizeRel now omit CMake-generated compiler debug information and link with `/DEBUG:NONE`. `/PDBALTPATH:moulang.pdb` records only a filename if linker debug information is enabled in another configuration. It does not sanitize a PDB's contents. Debug and RelWithDebInfo remain local development configurations.

When the installed compiler accepts `/experimental:deterministic` together with `/pathmap`, those flags map source and build roots to virtual relative paths in distribution configurations. CMake probes support before enabling them. They do not rewrite arbitrary string literals or already-built LLVM libraries. Check all actual outputs regardless of flag support.

```powershell
& ./tools/Check-ArtifactPrivacy.ps1 -Artifact @(
    'build/privacy-stage1/release/bin/moulang.exe',
    'build/privacy-stage1/release/bin/moulang_runtime.lib'
)
```

The scanner checks UTF-8 and aligned or unaligned UTF-16LE user-home paths. Optional `-ForbiddenPrefix` values reject known source or build roots elsewhere. Output contains counts rather than matching private values. `-ReportOnly` returns results without failing so an investigation can compare before and after. This is a string-pattern check, not proof that every possible personal datum or encoded secret is absent.

## Approved static toolchain

The owner approved LLVM-MinGW 20260616 (LLVM 22.1.8), UCRT, and Windows 10/11 x64. The compiler remains an MSVC/LLVM build, while the runtime is compiled separately with MinGW Clang. Generated COFF objects use the Windows GNU target ABI. C++ objects from the different toolchains are not mixed in the compiler executable.

The driver resolves ld.lld.exe, selected static archives, and startup objects relative to its own executable. MinGW's CRT initializes the selected libc++ support before calling the generated entry point. The produced program is a single executable with permitted Windows system DLL imports. The selected kit excludes standalone Microsoft SDK/MSVC development libraries. Source URLs and SHA-256 digests are pinned in tools/toolchains.json, and the installer verifies them before extraction or execution.

Keep the whole bin directory, including moulang.exe, moulang_runtime.lib, toolchain/bin/ld.lld.exe, toolchain/lib, and toolchain/licenses. Start from a fresh build/install directory so earlier private library copies cannot accidentally enter a release.

The previous standalone Microsoft library redistribution restriction motivated this replacement. Preserve the applicable LLVM and MinGW notices from third_party/licenses when distributing the kit or generated executables. See third_party/README.md for component scope, source revisions, and copyright attribution. A review of the exact package inventory and imports is required for every dependency update.

The UCRT-configured import archive is named libmsvcrt.a upstream. The archive name alone does not identify the final Windows DLL dependencies. An import-inspection test rejects non-system runtime DLLs, including shared libc++, libunwind, and Moulang runtimes.

A restricted-PATH test on a development PC is not a clean-OS installation test. Separate clean Windows 10/11 verification remains necessary before claiming compatibility without development tools.

## Local verification results

Latest final local results supersede the private Microsoft-kit observations below. Release passes nine CTest entries and Debug passes six, including the added package-manifest integrity regression. The SHA-256 installer rejects corrupt caches before extraction. Relocation and all three optimization levels preserve the tested outputs. PE import checks find only permitted Windows DLLs and verify the new kit contains no Microsoft standalone development libraries.

The Moulang compiler and its newly compiled runtime pass the zero-user-path gates. Unmodified supplier static archives still contain upstream automated CI paths. The raw scanner reports these matches, including six assertion-location strings remaining in libc++ even after a separate debug-strip experiment. Those supplier records are not the Moulang developer's machine paths. The distribution preserves the supplier archives and notices instead of silently editing diagnostic literals or claiming that every upstream path string is absent.

Verified with MSVC 19.51.36257, CMake 4.3.1, and the already installed LLVM 22.1.8 package. This stage did not download a new LLVM installation. Release and Debug were configured in separate new directories under `build/privacy-stage1`.

- Release build and all six CTest entries passed. They cover the existing compiler integration suite, privacy scanner fixtures, separate compiler/runtime artifact gates, relocation, and the compiled `__FILE__`/COFF object-name probe.
- The latest Debug build and all three applicable CTest entries passed, including relocation. The existing runtime optimization override warning is still emitted in Debug.
- The previous local Release runtime archive had 12 user-home path matches. The new archive has zero user-home or source-prefix matches. The previous compiler executable had one user-home match, from the configured linker path. Both compiler and runtime now pass the CTest artifact privacy gates after converting those literals to relative names.
- The inspected executable has a zero-sized PE debug directory and no RSDS marker. Removing PDB records alone cannot remove the configured linker literal.
- The standalone artifact privacy gate rejected the previous executable with its absolute linker literal. The current executable, runtime archive, and compiled probe pass. Any older scan reports under `build/privacy-stage1/reports` describe the revision that produced them and must not be treated as current release certification.
- Relocation tests copy the full compiler directory to a different Unicode/space path, restrict PATH to Windows System32, clear development tool discovery variables, and compare `hello.m`, `numbers.m`, and `objects.m` output. They check run, check, all three build optimization levels, and the diagnostic for an incomplete bundle. Visual Studio and the SDK still exist on the host, so this is not a clean-OS installation test.
- The existing VS Code host suite first failed because its Unicode diagnostic path acquired an unexpected space. A baseline comparison using the unchanged compiler passed. A subsequent run of the new compiler in that same comparison environment passed completion, execution, compiler diagnostics, and unsaved-edit checks. This transient failure remains a test-stability concern. No extension test assertions were relaxed.
- After the relative toolchain implementation, the existing VS Code host suite passed again using the new compiler. Source-only checks and the extension's execution/diagnostic workflow remain operational.
- A local CMake installation also executed Hello World with restricted tool discovery. A scan of all seven `.exe` and `.lib` files in that private installation found zero user-home or source-prefix matches. Results are saved under `build/privacy-stage1/reports/relative-bundle-path-scan.json`. This establishes the tested path-pattern result, not redistribution permission or clean-OS compatibility.
- The backup, email rewrite, and force-push instructions were reviewed against primary documentation but have not been executed against the owner's repositories. Recovery and remote history replacement remain manual, approval-gated operations.

## Sources

- [GitHub email privacy settings](https://docs.github.com/en/account-and-profile/how-tos/email-preferences/blocking-command-line-pushes-that-expose-your-personal-email-address)
- [GitHub history cleanup and limitations](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)
- [git-filter-repo manual](https://github.com/newren/git-filter-repo/blob/main/Documentation/git-filter-repo.txt)
- [Microsoft PDB path control](https://learn.microsoft.com/en-us/cpp/build/reference/pdbaltpath-use-alternate-pdb-path)
- [Microsoft debug information formats](https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format)
- [Microsoft licensing guidance, including standalone .lib restrictions](https://www.microsoft.com/licensing/guidance/Visual-Studio)
- [Visual Studio 2026 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution)
- [LLVM 22.1.8 license](https://github.com/llvm/llvm-project/blob/llvmorg-22.1.8/llvm/LICENSE.TXT)
