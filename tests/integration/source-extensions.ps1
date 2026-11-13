param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"

$suffixes = @('.mcx', '.m', '.mxx', '.mpp', '.mcp', '.m++', '.mc+')
$help = Invoke-TestProcess $Compiler '--help'
$expectedSuffixes = '  Moulang source extensions: ' + ($suffixes -join ' ')
if ($help.Code -ne 0 -or $help.Error -or
    ($help.Out -split '\r?\n') -cnotcontains $expectedSuffixes -or
    $help.Out -notmatch 'moulang run <source[.]mcx>') {
    throw "Unexpected source extension help: [$($help.Out)], [$($help.Error)]."
}

function Require-Success($Result, [string] $Operation, [string] $Expected = '') {
    if ($Result.Code -ne 0 -or $Result.Out -cne $Expected -or $Result.Error) {
        throw "Failed $Operation ($($Result.Code)): [$($Result.Out)], [$($Result.Error)]."
    }
}

$unicode = [char]0x03A9
$modules = (New-Item -ItemType Directory -Path (Join-Path $work 'source modules')).FullName
foreach ($suffix in $suffixes) {
    [IO.File]::WriteAllText((Join-Path $modules "helper $unicode$suffix"), 'int answer() { return 42 }', $utf8)
}

for ($i = 0; $i -lt $suffixes.Count; $i++) {
    $entry = "entry-$i $unicode" + $suffixes[$i]
    $importSuffix = $suffixes[($i + 1) % $suffixes.Count]
    $relative = "source modules/helper $unicode$importSuffix"
    $source = @"
import "$relative"
import "./$relative"
import "std:math"
main() { print(answer(), math.sqrt(1764)) }
"@
    $expected = '42 42' + [char]10
    Require-Success (Invoke-Moulang $entry $source 'check') "check $entry"
    Require-Success (Invoke-Moulang $entry $source) "run $entry" $expected
    Require-Success (Invoke-Moulang $entry $source 'build') "default build $entry"
    $defaultOutput = Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($entry) + '.exe')
    Require-Success (Invoke-TestProcess $defaultOutput) "run default executable for $entry" $expected
    $explicitOutput = Join-Path $work "custom output-$i.exe"
    Require-Success (Invoke-Moulang $entry $source 'build' -Options @('-o', ('"' + $explicitOutput + '"'))) "build -o $entry"
    Require-Success (Invoke-TestProcess $explicitOutput) "run explicit executable for $entry" $expected

    $library = ('import "' + $relative + '"') + [char]10 + 'int checkLibrary() { return answer() }'
    $overlay = Invoke-Moulang $entry 'invalid source on disk' 'check' -InputBytes $utf8.GetBytes($library) -Options @('--stdin', '--library')
    Require-Success $overlay "check unsaved library $entry"
}

[IO.File]::WriteAllText((Join-Path $work 'upper.m'), 'int value() { return 1 }', $utf8)
foreach ($bad in 'upper.M', 'near.m+', 'near.mc++', 'unsupported.txt', 'C:/absolute.mcx') {
    $source = ('import "' + $bad + '"') + [char]10 + 'main() {}'
    $result = Invoke-Moulang 'invalid-import.mcx' $source 'check'
    if ($result.Code -ne 1 -or $result.Error -notmatch 'invalid-import[.]mcx:1:\d+: error ML3028: Import requires a relative path') {
        throw "Unexpected import diagnostic for $($bad): [$($result.Error)]."
    }
}

$source = 'import "missing.mc+"' + [char]10 + 'main() {}'
$missing = Invoke-Moulang 'missing-source.mcx' $source 'check'
if ($missing.Code -ne 1 -or $missing.Error -notmatch 'error ML3028: Cannot open source file') {
    throw "Expected a missing-file diagnostic for .mc+: [$($missing.Error)]."
}
[IO.File]::WriteAllText((Join-Path $work 'cycle.m++'), 'import "cycle.mcx"', $utf8)
$cycle = Invoke-Moulang 'cycle.mcx' ('import "cycle.m++"' + [char]10 + 'main() {}') 'check'
if ($cycle.Code -ne 1 -or $cycle.Error -notmatch 'cycle[.]m\+\+:1:\d+: error ML3029:') {
    throw "Expected a mixed-suffix cycle diagnostic: [$($cycle.Error)]."
}

[IO.File]::WriteAllText((Join-Path $work 'physical.M'), 'int value() { return 42 }', $utf8)
$caseSource = 'import "physical.m"' + [char]10 + 'main() { print(value()) }'
$caseResult = Invoke-Moulang 'case-path.mcx' $caseSource
if ([IO.File]::Exists((Join-Path $work 'physical.m'))) {
    Require-Success $caseResult 'lowercase import on a case-insensitive directory' ('42' + [char]10)
} elseif ($caseResult.Code -ne 1 -or $caseResult.Error -notmatch 'error ML3028: Cannot open source file') {
    throw "Expected the filesystem to enforce case: [$($caseResult.Error)]."
}
Require-Success (Invoke-Moulang 'explicit.txt' 'main() {}' 'check') 'explicit CLI path with a nonstandard suffix'

$modernModule = @'
private struct Hidden { int value }
private int hiddenHelper() { return Hidden(10).value }
public struct ModuleRecord { int value }
public int modernValue() { return hiddenHelper() }
'@
$legacyModule = @'
import "modern.mcx"
private struct Hidden { int value }
private int hiddenHelper() { return Hidden(32).value }
public int legacyValue() { return hiddenHelper() }
'@
[IO.File]::WriteAllText((Join-Path $modules 'modern.mcx'), $modernModule, $utf8)
[IO.File]::WriteAllText((Join-Path $modules 'legacy.m'), $legacyModule, $utf8)
Require-Success (Invoke-Moulang 'source modules/legacy.m' $legacyModule 'check' -Options @('--library')) 'check source module without main'

$moduleImport = 'import "source modules/legacy.m"' + [char]10
$entry = $moduleImport + @'
main() {
    print(modernValue(), legacyValue())
    print("values {} {}") { format(modernValue(), legacyValue()) }
}
'@
$moduleOutput = @('10 32', 'values 10 32', '') -join [char]10
foreach ($suffix in '.mcx', '.m') {
    Require-Success (Invoke-Moulang ('module-entry' + $suffix) $entry) "run public source modules from $suffix" $moduleOutput
}

[IO.File]::WriteAllText((Join-Path $modules 'function-conflict.mcx'), 'public int modernValue(int input) { return input }', $utf8)
[IO.File]::WriteAllText((Join-Path $modules 'type-conflict.m'), 'public struct ModuleRecord { int other }', $utf8)
[IO.File]::WriteAllText((Join-Path $modules 'private-api.mcx'),
    ('private struct Hidden {}' + [char]10 + 'public Hidden expose() { return Hidden() }'), $utf8)
[IO.File]::WriteAllText((Join-Path $modules 'cycle-hop.m'), 'import "cycle-tail.mcp"', $utf8)
[IO.File]::WriteAllText((Join-Path $modules 'cycle-tail.mcp'), 'import "../cycle-root.mcx"', $utf8)

foreach ($case in @(
    @{ File = 'private-function.mcx'; Source = $moduleImport + 'main() { print(hiddenHelper()) }';
        Error = 'private-function[.]mcx:2:\d+: error ML3010:' },
    @{ File = 'private-type.m'; Source = $moduleImport + 'main() { Hidden value = Hidden(1) }';
        Error = 'private-type[.]m:2:\d+: error ML3021:' },
    @{ File = 'duplicate-function.m'; Source = $moduleImport + 'import "source modules/function-conflict.mcx"' + [char]10 + 'main() {}';
        Error = 'function-conflict[.]mcx:1:\d+: error ML3017:' },
    @{ File = 'duplicate-type.mcx'; Source = $moduleImport + 'import "source modules/type-conflict.m"' + [char]10 + 'main() {}';
        Error = 'type-conflict[.]m:1:\d+: error ML3024:' },
    @{ File = 'private-export.m'; Source = 'import "source modules/private-api.mcx"' + [char]10 + 'main() {}';
        Error = 'private-api[.]mcx:2:\d+: error ML3031:' },
    @{ File = 'cycle-root.mcx'; Source = 'import "source modules/cycle-hop.m"' + [char]10 + 'main() {}';
        Error = 'cycle-tail[.]mcp:1:\d+: error ML3029:' }
)) {
    $result = Invoke-Moulang $case.File $case.Source 'check'
    if ($result.Code -ne 1 -or $result.Out -or $result.Error -notmatch $case.Error) {
        throw "Expected $($case.Error) for $($case.File), got [$($result.Error)]."
    }
}
Write-Output 'PASS: source suffixes, CLI outputs, mixed imports, visibility, public name conflicts, and cycles.'
