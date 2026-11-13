param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $ReadObject,
    [Parameter(Mandatory = $true)] [string] $Examples,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
$root = New-Item -ItemType Directory -Force -Path $TestRoot
$workspace = (New-Item -ItemType Directory -Path (Join-Path $root.FullName ([guid]::NewGuid().ToString('N')))).FullName
$env:TEMP = $workspace
$env:TMP = $workspace
$system = @('KERNEL32.dll', 'ADVAPI32.dll', 'SHELL32.dll', 'USER32.dll', 'msvcrt.dll', 'ucrtbase.dll', 'ntdll.dll')
$crt = @('convert', 'environment', 'filesystem', 'heap', 'locale', 'math', 'private', 'runtime', 'stdio', 'string') |
    ForEach-Object { "api-ms-win-crt-$_-l1-1-0.dll" }

function Assert-SystemImports([string] $File, [string[]] $Allowed) {
    $imports = & $ReadObject --coff-imports $File
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect PE imports.' }
    $dlls = @($imports | ForEach-Object { if ($_ -match '^\s*Name:\s*(\S+\.dll)\s*$') { $Matches[1] } })
    if (!$dlls.Count) { throw 'PE import inspection returned no DLLs.' }
    $name = Split-Path -Leaf $File
    $unexpected = @($dlls | Where-Object { $_ -notin $Allowed } | Sort-Object -Unique)
    if ($unexpected.Count) {
        throw "Unexpected DLL imports in $name ($($unexpected -join ', '))."
    }
    Write-Output "PASS: $name imports $((($dlls | Sort-Object -Unique) -join ', '))."
}

Assert-SystemImports $Compiler ($system + 'ole32.dll')
foreach ($name in 'hello', 'numbers', 'objects', 'text', 'lists', 'standard-library') {
    $exe = Join-Path $workspace ($name + '.exe')
    & $Compiler build (Join-Path $Examples ($name + '.m')) -o $exe
    if ($LASTEXITCODE -ne 0) { throw "Cannot build import fixture $name." }
    Assert-SystemImports $exe ($system + $crt)
}
$bin = Split-Path -Parent $Compiler
foreach ($file in 'libcmt.lib', 'libcpmt.lib', 'libvcruntime.lib', 'kernel32.lib') {
    if (Test-Path -LiteralPath (Join-Path $bin ('toolchain/lib/' + $file))) {
        throw 'A Microsoft standalone development library is present in the new kit.'
    }
}
foreach ($file in 'LLVM-LICENSE.txt', 'libcxx-LICENSE.txt', 'libunwind-LICENSE.txt', 'compiler-rt-LICENSE.txt',
    'MinGW-COPYING.txt', 'MinGW-runtime-COPYING.txt') {
    if (!(Test-Path -LiteralPath (Join-Path $bin ('toolchain/licenses/' + $file)))) {
        throw "Missing required third-party notice: $file"
    }
}
Write-Output 'PASS: no Microsoft standalone link libraries; all selected license notices are included.'
