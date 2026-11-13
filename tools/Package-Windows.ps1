param(
    [string] $BuildDirectory = 'build/windows-release',
    [string] $OutputDirectory = 'build/release'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    $output = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName
    $stage = Join-Path $output ('staging-' + [guid]::NewGuid().ToString('N'))
    $cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    $cmake = (Select-String -LiteralPath $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$').Matches[0].Groups[1].Value
    $compilerVersion = (Select-String -LiteralPath $cache -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$').Matches[0].Groups[1].Value
    & $cmake --install $BuildDirectory --prefix $stage --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Package installation staging failed.' }
    $extension = Get-Content -Raw -LiteralPath vscode-extension/package.json | ConvertFrom-Json
    $vsix = Join-Path $stage ('moulang-' + $extension.version + '.vsix')
    Push-Location vscode-extension
    try {
        & npm.cmd exec -- vsce package --no-dependencies --allow-missing-repository --out $vsix
        if ($LASTEXITCODE -ne 0) { throw 'VSIX packaging failed.' }
    } finally { Pop-Location }
    $null = New-Item -ItemType Directory -Path (Join-Path $stage 'examples')
    $examples = foreach ($example in 'examples/hello.mcx', 'examples/hello.m') {
        $destination = Join-Path $stage $example
        Copy-Item -LiteralPath $example -Destination $destination
        $destination
    }
    Copy-Item -LiteralPath LICENSE -Destination $stage
    Copy-Item -LiteralPath docs/INSTALL.md -Destination (Join-Path $stage 'INSTALL.md')
    Copy-Item -LiteralPath tools/Verify-Install.ps1 -Destination $stage
    $vsixContents = Join-Path $output ('inspect-vsix-' + [guid]::NewGuid().ToString('N'))
    [IO.Compression.ZipFile]::ExtractToDirectory($vsix, $vsixContents)
    $projectFiles = @(
        (Join-Path $stage 'bin/moulang.exe'), (Join-Path $stage 'bin/moulang_runtime.lib'),
        (Join-Path $stage 'LICENSE'),
        (Join-Path $stage 'INSTALL.md'), (Join-Path $stage 'Verify-Install.ps1')
    ) + $examples + @(Get-ChildItem -LiteralPath $vsixContents -Recurse -File | ForEach-Object FullName)
    $null = & ./tools/Check-ArtifactPrivacy.ps1 -Artifact $projectFiles -ForbiddenPrefix $repo
    $null = & ./tools/Check-ArtifactPrivacy.ps1 -Artifact (Join-Path $stage 'bin/toolchain/bin/ld.lld.exe') -ForbiddenPrefix $repo
    $files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object {
        [pscustomobject]@{ path = $_.FullName.Substring($stage.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    [pscustomobject]@{ compilerVersion = $compilerVersion; extensionVersion = $extension.version; files = $files } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') -Encoding utf8
    $zip = Join-Path $output 'moulang-windows-x64.zip'
    if (Test-Path -LiteralPath $zip) { throw 'Use a fresh output directory to preserve previous packages.' }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($zip + '.sha256', $hash + '  moulang-windows-x64.zip' + "`n", [Text.UTF8Encoding]::new($false))
    Write-Output "Created moulang-windows-x64.zip and its SHA-256 file."
}
finally { Pop-Location }
