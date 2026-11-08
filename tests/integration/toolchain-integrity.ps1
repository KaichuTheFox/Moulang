param(
    [Parameter(Mandatory = $true)] [string] $Installer,
    [Parameter(Mandatory = $true)] [string] $LockFile,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
$lock = Get-Content -Raw -LiteralPath $LockFile | ConvertFrom-Json
$root = New-Item -ItemType Directory -Force -Path $TestRoot
foreach ($name in 'llvm', 'mingw') {
    $cache = (New-Item -ItemType Directory -Path (Join-Path $root.FullName ([guid]::NewGuid().ToString('N')))).FullName
    [IO.File]::WriteAllBytes((Join-Path $cache $lock.$name.archive), [byte[]] @(0, 1, 2))
    $rejected = $false
    try { $null = & $Installer -Component $name -Destination $cache }
    catch {
        if ($_.Exception.Message -notmatch 'wrong SHA-256') { throw }
        $rejected = $true
    }
    if (!$rejected -or (Test-Path -LiteralPath (Join-Path $cache $lock.$name.directory))) {
        throw "The installer accepted or extracted a corrupt $name archive."
    }
}
Write-Output 'PASS: both toolchain archives are rejected before extraction when their SHA-256 is wrong.'
