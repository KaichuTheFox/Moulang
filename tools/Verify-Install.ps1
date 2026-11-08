param([string] $Root = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath $Root).Path
$manifest = Get-Content -Raw -LiteralPath (Join-Path $rootPath 'package-manifest.json') | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $path = [IO.Path]::GetFullPath((Join-Path $rootPath $entry.path))
    if (!$path.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The package manifest contains a path outside the installation.'
    }
    if (!(Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $entry.sha256) {
        throw "Package integrity mismatch for $($entry.path)."
    }
}
$actualCount = @(Get-ChildItem -LiteralPath $rootPath -File -Recurse).Count
if ($actualCount -ne @($manifest.files).Count + 1) { throw 'The package has unlisted files.' }
$visualStudioPresent = Test-Path -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe')
Write-Output "Visual Studio installer present on this host: $visualStudioPresent. Tool discovery will be restricted."
$env:PATH = Join-Path $env:SystemRoot 'System32'
foreach ($name in 'LIB', 'LIBPATH', 'INCLUDE', 'LLVM_DIR', 'VSINSTALLDIR', 'VCINSTALLDIR',
    'VCToolsInstallDir', 'WindowsSdkDir', 'WindowsSDKVersion') {
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}
$workspace = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ('moulang-install-' + [guid]::NewGuid().ToString('N')))).FullName
$env:TEMP = $workspace
$env:TMP = $workspace
$compiler = Join-Path $rootPath 'bin/moulang.exe'
$output = & $compiler run (Join-Path $rootPath 'examples/hello.m') 2>&1
if ($LASTEXITCODE -ne 0 -or ($output -join "`n") -cne 'Hello Moulang!') {
    throw ('The installed compiler failed to compile and run Hello World. ' + ($output -join "`n"))
}
Write-Output 'PASS: package hashes and Hello World with restricted development-tool discovery.'
Write-Output 'A hosted runner may still contain development tools. Also validate on clean Windows 10/11.'
