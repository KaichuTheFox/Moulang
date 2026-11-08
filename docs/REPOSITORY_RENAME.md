# Repository name transition checklist

The owner completed the server-side rename to https://github.com/KaichuTheFox/Moulang. The 0.2.0 release preparation verifies this canonical repository and updates this clone's origin URL. Relative documentation, extension metadata and workflow repository contexts retain their intended targets. No history rewrite is part of this transition. The checklist below remains a reference for other clones and external consumers.

## Inventory

- README.md: the release download URL already targets Moulang. Remove its temporary rename caveat only after checking the final repository and published release.
- vscode-extension/package.json: repository.url, homepage, and bugs.url already target Moulang. Display name is Moulang for VS Code. Keep publisher KaichuTheFox and name moulang; a repository rename does not require another extension identity change.
- vscode-extension/README.md: compiler setup and issue links already target Moulang. Remove its temporary caveat after the final URLs resolve. A Marketplace upload remains a separate owner action.
- docs: internal documentation links are relative and remain valid. The privacy guide obtains the current remote URL dynamically. Toolchain download URLs and third-party attribution URLs belong to their upstream owners and must not be rewritten.
- tests/vscode/suite.js: metadata assertions use the final repository, homepage, and issue URLs. tests/docs/repository-links.test.js checks project-owned URL consistency and relative Markdown document links.
- .github/workflows/windows.yml: checkout uses the current repository context, and the release command uses github.repository. These adapt to the renamed repository. No hardcoded project repository URL or CI badge exists in this workflow or README.
- If adding a CI badge later, use https://github.com/KaichuTheFox/Moulang/actions/workflows/windows.yml/badge.svg and link it to the same workflow without badge.svg. Check external badges, websites, download instructions, webhooks, and reusable-workflow consumers separately.
- Git origin: update every local clone manually after the server-side rename. Backups and existing release archives retain their original provenance and are not rewritten by this step.

## Owner procedure

1. Review the backup procedure in PRIVACY.md and coordinate with collaborators. Repository naming and email history replacement are separate operations.
2. On the current GitHub repository page, open Settings, edit Repository name to Moulang, and click Rename. Confirm the destination page and its permissions, issues, releases, and Actions settings.
3. From each working clone, record the old URL privately, then run the following commands yourself:

```powershell
$oldOrigin = (git remote get-url origin).Trim()
git remote set-url origin https://github.com/KaichuTheFox/Moulang.git
if ($LASTEXITCODE -ne 0) { throw 'Remote URL update failed.' }
git remote get-url origin
git ls-remote origin HEAD refs/heads/main
if ($LASTEXITCODE -ne 0) { throw 'Check repository access and authentication.' }
```

This changes a clone's URL, not its local remote alias. No force push or file deletion is required for the rename. Keep the privately recorded old URL for diagnosis.

4. Check the README release URL, extension source/homepage/issue links, relative docs, and Actions workflow page. Run `node --test tests/docs/repository-links.test.js` and the extension tests. The local URL test checks consistency and local file existence, not network availability.
5. Run the CI workflow and installation check at the final location. Publish a release or extension update only through the separate reviewed procedure. Confirm Marketplace metadata after any owner-performed upload.

GitHub redirects most web and Git traffic after a rename, but project Pages URLs and consumers of an action hosted in the renamed repository need separate attention. Do not reuse the previous repository name for a different repository if its redirects are needed. See [GitHub's rename guide](https://docs.github.com/en/repositories/creating-and-managing-repositories/renaming-a-repository) and [remote URL guide](https://docs.github.com/en/get-started/git-basics/managing-remote-repositories).
