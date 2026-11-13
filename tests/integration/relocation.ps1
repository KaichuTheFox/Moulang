param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $Examples,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)

$ErrorActionPreference = 'Stop'
$root = New-Item -ItemType Directory -Force -Path $TestRoot
$workspace = New-Item -ItemType Directory -Path (Join-Path $root.FullName ([guid]::NewGuid().ToString('N')))
$moved = Join-Path $workspace.FullName ('relocated toolchain ' + [char]0x03A9)
Copy-Item -LiteralPath (Split-Path -Parent $Compiler) -Destination $moved -Recurse
$movedCompiler = Join-Path $moved 'moulang.exe'
$incomplete = (New-Item -ItemType Directory -Path (Join-Path $workspace.FullName 'incomplete')).FullName
Copy-Item -LiteralPath $movedCompiler -Destination (Join-Path $incomplete 'moulang.exe')
Copy-Item -LiteralPath (Join-Path $moved 'moulang_runtime.lib') -Destination $incomplete

$sampleFiles = @('hello.mcx', 'hello.m', 'numbers.m', 'objects.m', 'standard-library.m')
foreach ($file in $sampleFiles) {
    Copy-Item -LiteralPath (Join-Path $Examples $file) -Destination $workspace.FullName
}
$reference = @{}
$env:TEMP = $workspace.FullName
$env:TMP = $workspace.FullName
foreach ($file in $sampleFiles) {
    $lines = & $Compiler run (Join-Path $workspace.FullName $file) 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Reference execution failed for $file." }
    $reference[$file] = $lines -join "`n"
}

# This process is a CTest child. Restrict discovery without changing the machine.
$env:PATH = Join-Path $env:SystemRoot 'System32'
foreach ($name in 'LIB', 'LIBPATH', 'INCLUDE', 'LLVM_DIR', 'VSINSTALLDIR', 'VCINSTALLDIR',
    'VCToolsInstallDir', 'WindowsSdkDir', 'WindowsSDKVersion') {
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}
$env:TEMP = $workspace.FullName
$env:TMP = $workspace.FullName
Push-Location $workspace.FullName
try {
    foreach ($file in $sampleFiles) {
        $lines = & $movedCompiler run $file 2>&1
        if ($LASTEXITCODE -ne 0 -or ($lines -join "`n") -cne $reference[$file]) {
            throw "Relocated execution failed for $file."
        }
        & $movedCompiler check $file
        if ($LASTEXITCODE -ne 0) { throw "Relocated source check failed for $file." }
        foreach ($level in '-O0', '-O2', '-O3') {
            $executable = Join-Path $workspace.FullName ($file + $level + '.exe')
            & $movedCompiler build $file $level -o $executable
            if ($LASTEXITCODE -ne 0) { throw "Relocated build failed for $file $level." }
            $lines = & $executable 2>&1
            if ($LASTEXITCODE -ne 0 -or ($lines -join "`n") -cne $reference[$file]) {
                throw "Relocated program failed for $file $level."
            }
        }
    }
    $missingCompiler = Join-Path $incomplete 'moulang.exe'
    # Capture native stderr as data under Windows PowerShell as well as PowerShell 7.
    $ErrorActionPreference = 'Continue'
    $missing = & $missingCompiler run hello.m 2>&1 | ForEach-Object { $_.ToString() }
    $missingCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($missingCode -ne 1 -or ($missing -join "`n") -notmatch 'Missing bundled toolchain file') {
        throw 'An incomplete bundle did not report the expected diagnostic.'
    }
}
finally { Pop-Location }
Write-Output 'PASS: relocated Unicode/space path, restricted tool discovery, run/check/build, all optimization levels, numeric and class examples, incomplete bundle diagnostic.'
