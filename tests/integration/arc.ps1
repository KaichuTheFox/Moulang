param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $Cxx,
    [Parameter(Mandatory)] [string] $TestRoot,
    [Parameter(Mandatory)] [string] $Configuration
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$env:MOULANG_ARC_REPORT = '1'

function Invoke-ArcExecutable([string] $Program, [string] $Argument = '') {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.Arguments = $Argument
    $start.WorkingDirectory = $work
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $process = [Diagnostics.Process]::Start($start)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return @{ Code = $process.ExitCode; Out = $output.GetAwaiter().GetResult().TrimEnd(); Error = $errors.GetAwaiter().GetResult().TrimEnd() }
    } finally {
        $process.Dispose()
    }
}

function Test-Program([string] $Name, [string] $Level, [string] $Expected, [int] $Roots) {
    $output = Join-Path $work ($Name + $Level + '.exe')
    & $Compiler build (Join-Path $PSScriptRoot "$Name.m") -o $output $Level
    if ($LASTEXITCODE -ne 0) { throw "Failed to build $Name with $Level." }
    $r = Invoke-ArcExecutable $output
    $expectedError = if ($Configuration -eq 'Debug') {
        $rooted = if ($Roots) { '[1-9]\d* rooted_bytes=[1-9]\d*' } else { '0 rooted_bytes=0' }
        "^Moulang ARC debug: live_objects=0 live_bytes=0 class_roots=$Roots rooted_objects=$rooted$"
    } else { '^$' }
    if ($r.Code -ne 0 -or $r.Out -cne $Expected -or $r.Error -notmatch $expectedError) {
        throw "Unexpected $Name $Level result: $($r.Code), [$($r.Out)], [$($r.Error)]."
    }
}

$expected = @'
ab new!
hello new! no! yes!
right! left! new!
left! right!
item new!
0
ell
21
'@
$expected = $expected.Replace([string][char]13, '')
foreach ($level in '-O0', '-O2', '-O3') {
    Test-Program 'arc-lifetimes' $level $expected 1
}
Test-Program 'arc-stress' '-O2' '1000000 50000 0' 0

$overBudget = Invoke-Moulang 'arc-budget.m' 'main() { print("x" * 268435456) }'
if ($overBudget.Code -ne 1 -or $overBudget.Error -notmatch 'dynamic text and collection memory limit exceeded') {
    throw "The live budget did not reject the oversized allocation: $($overBudget.Error)"
}
if ($Configuration -eq 'Debug') {
    $native = Join-Path $work 'arc-debug.exe'
    $archive = Join-Path (Split-Path -Parent $Compiler) 'moulang_runtime.lib'
    & $Cxx -std=c++20 -static (Join-Path $PSScriptRoot 'arc-debug.cpp') $archive -o $native
    if ($LASTEXITCODE -ne 0) { throw 'Cannot build ARC debug fixture.' }
    $leak = Invoke-ArcExecutable $native 'leak'
    if ($leak.Code -ne 0 -or $leak.Error -notmatch 'live_objects=1 live_bytes=[1-9]\d* class_roots=0') {
        throw "Expected one reported leak: $($leak.Error)"
    }
    foreach ($test in 'stale', 'stale-text') {
        $stale = Invoke-ArcExecutable $native $test
        if ($stale.Code -ne 1 -or $stale.Error -notmatch 'stale or invalid ownership handle' -or
            $stale.Error -notmatch 'aborted before normal leak report') {
            throw "Expected stale handle rejection for $test ($($stale.Error))."
        }
    }
    $stale = Invoke-ArcExecutable $native 'stale-iterator'
    if ($stale.Code -ne 1 -or $stale.Error -notmatch 'list structure changed during foreach' -or
        $stale.Error -notmatch 'aborted before normal leak report') {
        throw "Expected stale iterator rejection ($($stale.Error))."
    }
}
Write-Output 'PASS: ARC lifetimes, budget reuse, and configuration-specific memory reports.'
