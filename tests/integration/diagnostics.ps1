param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$rich = '--diagnostic-format=rich'
$plain = '--diagnostic-format=plain'
$lf = [string][char]10

function Get-DiagnosticLines($Result) {
    if ($Result.Code -ne 1 -or $Result.Out) {
        throw "Expected compiler failure and empty stdout. Got $($Result.Code), [$($Result.Out)], [$($Result.Error)]."
    }
    return $Result.Error.Replace([string][char]13, '').TrimEnd([char]10).Split([char]10)
}

$cases = @(
    @{ Source = 'main() { print(missing) }'; Header = ":1:16: error ML3004: Undeclared variable 'missing'."; Mark = 'missing'; Detail = 'Declare the variable in a visible scope or check its spelling.' },
    @{ Source = 'main() { int n = true }'; Header = ':1:14: error ML3007: Variable and expression types must match.'; Mark = 'true'; Detail = "'n' expects int, but the value has type bool." },
    @{ Source = 'main() { print("\q") }'; Header = ':1:17: error ML1004: Unknown string escape. Use a supported escape or escape the backslash.'; Mark = '\q'; Detail = 'Write \\ for a literal backslash.' },
    @{ Source = 'main() { print(9223372036854775808) }'; Header = ':1:16: error ML3003: Integer literal is outside the signed 64-bit range.'; Mark = '9223372036854775808'; Detail = 'The maximum long value is 9223372036854775807.' },
    @{ Source = 'main() { print(long(1) + 0.5) }'; Header = ':1:24: error ML3005: Mixed long and real values require an explicit conversion.'; Mark = '+'; Detail = 'This operator combines long and double.' },
    @{ Source = 'main() { if (1) {} }'; Header = ':1:10: error ML3014: A condition must have type bool.'; Mark = '1'; Detail = 'The condition has type int.' },
    @{ Source = 'main() { print(noSuchFunction()) }'; Header = ':1:16: error ML3010: Unknown callable function.'; Mark = 'noSuchFunction'; Detail = "No function named 'noSuchFunction' is visible here." },
    @{ Source = 'main() { var x = null }'; Header = ':1:14: error ML3019: Specify a nullable type when initializing with null.'; Mark = 'null'; Detail = 'A null initializer does not determine the underlying type.' },
    @{ Source = 'main() { int a[2] = [1] }'; Header = ':1:21: error ML3032: Fixed array initializer length does not match the declaration.'; Mark = '[1]'; Detail = 'Expected 2 elements, but received 1.' },
    @{ Source = 'main() { print(int("123")) }'; Header = ':1:16: error ML3020: A numeric cast requires exactly one numeric value.'; Mark = '"123"'; Detail = 'This argument has type str.' }
)
foreach ($case in $cases) {
    $file = 'source with spaces.m'
    $expected = (Join-Path $work $file) + $case.Header
    foreach ($options in @(@{ Args = @() }, @{ Args = @($plain) })) {
        $lines = @(Get-DiagnosticLines (Invoke-Moulang $file $case.Source 'check' -Options $options.Args))
        if ($lines.Count -ne 1 -or $lines[0] -cne $expected) { throw "Plain output changed: $lines" }
    }
    $lines = @(Get-DiagnosticLines (Invoke-Moulang $file $case.Source 'check' -Options $rich))
    $marker = '    | ' + (' ' * $case.Source.IndexOf($case.Mark)) + '^' + ('~' * ($case.Mark.Length - 1))
    if ($lines[0] -cne $expected -or $lines[1] -cne ('  1 | ' + $case.Source) -or $lines[2] -cne $marker -or
        !(($lines -join $lf).Contains($case.Detail))) {
        throw "Incorrect rich output: $($lines -join $lf)"
    }
}

foreach ($command in 'build', 'run') {
    $lines = @(Get-DiagnosticLines (Invoke-Moulang 'invalid.m' $cases[0].Source $command -Options $rich))
    if ($lines[0] -notmatch 'ML3004' -or $lines[1] -notmatch 'print\(missing\)') { throw "$command lost rich context." }
}

$overlay = 'main() { print(overlayName) }'
$r = Invoke-Moulang 'overlay.m' 'main() { print("on disk") }' 'check' $utf8.GetBytes([string][char]0xFEFF + $overlay) -Options @('--stdin', $rich)
$lines = @(Get-DiagnosticLines $r)
if ($lines[1] -cne ('  1 | ' + $overlay) -or $r.Error.Contains('on disk')) { throw 'Expected unsaved source context.' }
$invalid = @(Get-DiagnosticLines (Invoke-Moulang 'encoding.m' 'main() {}' 'check' ([byte[]] @(255)) -Options @('--stdin', $rich)))
if ($invalid.Count -ne 1 -or $invalid[0] -notmatch 'ML1000') { throw 'Invalid UTF-8 must not render source context.' }

$dependency = 'int imported() { return importedMissing }'
[IO.File]::WriteAllText((Join-Path $work 'dep.m'), $dependency, $utf8)
$entry = 'import "dep.m"' + $lf + 'main() {}'
$lines = @(Get-DiagnosticLines (Invoke-Moulang 'entry.m' $entry 'check' -Options $rich))
if ($lines[0] -notmatch 'dep\.m:1:.*ML3004' -or $lines[1] -cne ('  1 | ' + $dependency)) {
    throw "Incorrect imported source: $($lines -join $lf)"
}

$unicode = [char]0xD55C + [char]::ConvertFromUtf32(0x1F600) + 'e' + [char]0x301 + [char]27
$source = "main() {`r`n`tprint(""$unicode"", missing)`r`n}"
$lines = @(Get-DiagnosticLines (Invoke-Moulang 'unicode.m' $source 'check' -Options $rich))
$rendered = '    print("' + [char]0xD55C + '\u{1f600}e\u{301}\u{1b}", missing)'
$visualColumn = $rendered.IndexOf('missing') + 1
if ($lines[0] -notmatch ':2:17: error ML3004' -or $lines[1] -cne ('  2 | ' + $rendered) -or
    $lines[2] -cne ('    | ' + (' ' * $visualColumn) + '^~~~~~~')) {
    throw "Incorrect Unicode position or unsafe excerpt: $($lines -join $lf)"
}

$source = 'main() { print("' + ('x' * 400) + '", missing) }'
$lines = @(Get-DiagnosticLines (Invoke-Moulang 'clipped.m' $source 'check' -Options $rich))
if ($lines[1] -notmatch '^\s+1 \| \.\.\..*missing' -or $lines[2] -notmatch '\^~~~~~~') { throw 'Clipping hid the highlight.' }
$lines = @(Get-DiagnosticLines (Invoke-Moulang 'eof.m' 'main() {' 'check' -Options $rich))
if ($lines[2] -cne ('    | ' + (' ' * 8) + '^')) { throw 'EOF caret is not at the end of the source.' }

$hangulName = ([char]0xC810, [char]0xC218, [char]0xAC12) -join ''
$hangulTypo = ([char]0xC810, [char]0xC218, [char]0xAC11) -join ''
$suggestions = @(
    @{ Source = "main() {`n    int count = 1`n    print(coun)`n}"; Code = 'ML3004'; Suggest = 'count' },
    @{ Source = "main() {`n    int cat = 1`n    int bat = 2`n    print(mat)`n}"; Code = 'ML3004'; Suggest = '' },
    @{ Source = "main() {`n    int $hangulName = 1`n    print($hangulTypo)`n}"; Code = 'ML3004'; Suggest = $hangulName },
    @{ Source = "int calculate() { return 1 }`nmain() { print(calculat()) }"; Code = 'ML3010'; Suggest = 'calculate' },
    @{ Source = 'import "private.m"' + $lf + 'main() { print(calculat()) }'; Code = 'ML3010'; Suggest = '' },
    @{ Source = "int calculate(int value) { return value }`nmain() { print(calculate(""bad"")) }"; Code = 'ML3011'; Suggest = '' }
)
[IO.File]::WriteAllText((Join-Path $work 'private.m'), 'private int calculate() { return 1 }', $utf8)
foreach ($case in $suggestions) {
    $lines = @(Get-DiagnosticLines (Invoke-Moulang 'suggestions.m' $case.Source 'check' -Options $rich))
    $text = $lines -join $lf
    if ($lines[0] -notmatch $case.Code) { throw "Unexpected name resolution diagnostic: $text" }
    if ($case.Suggest) {
        if (!$text.Contains("Did you mean '$($case.Suggest)'?")) { throw "Suggestion missing: $text" }
    } elseif ($text.Contains('Did you mean')) { throw "Unexpected suggestion: $text" }
}

$badOption = Invoke-Moulang 'options.m' 'main() {}' 'check' -Options @($rich, $plain)
if ($badOption.Code -ne 2) { throw 'Conflicting diagnostic options must fail usage validation.' }
Write-Output 'PASS: plain and rich diagnostics, source snapshots, Unicode positions, and visible name suggestions.'
