param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"

$source = @'
import "module.m"
import "std:text" as strings
int sample(int ignoredParameter) {
    int written = 0
    written = 2
    int inputOnly = 0
    print("input") { input(inputOnly) }
    int _ignored = 0
    int typed = 42
    print(type(typed))
    if (true) {
        int sibling = 1
        print(sibling)
    } else {
        int sibling = 2
    }
    return 1
}
main() {
    str text = "\u{D55C}\u{AE00}"
    str result = ""
    str prepended = ""
    foreach (part in text) {
        result += part
        prepended = part + prepended
    }
    for (i=0 in i++(strings.len(text))) {
        print(text[i])
    }
    print(result, prepended, answer())
    print("value {}") { format(sample(1)) }
}
'@
[IO.File]::WriteAllText((Join-Path $work 'module.m'), 'int answer() { int ignored = 0' + [char]10 + 'return 42 }', $utf8)

function Assert-Warnings($Result, [string[]] $Expected) {
    $actual = @($Result.Error -split '\r?\n' | Where-Object { $_ } | ForEach-Object {
        if ($_ -notmatch '([^\\/]+:\d+:\d+: warning TI-[BP]\d{3}): .+ Hint: .+') {
            throw "Unexpected diagnostic: [$_]"
        }
        $Matches[1]
    })
    if ($Result.Code -ne 0 -or $Result.Out -or ($actual -join '|') -cne ($Expected -join '|')) {
        throw "Expected [$($Expected -join ', ')], got code $($Result.Code), [$($Result.Out)], [$($Result.Error)]."
    }
}

$basic = @(
    'positive.mcx:4:9: warning TI-B001',
    'positive.mcx:6:9: warning TI-B001',
    'positive.mcx:15:13: warning TI-B001',
    'module.m:1:20: warning TI-B001'
)
$perf = @(
    'positive.mcx:24:9: warning TI-P001',
    'positive.mcx:25:9: warning TI-P001',
    'positive.mcx:28:19: warning TI-P002'
)
$all = @($basic[0..2]) + $perf + @($basic[3])
Assert-Warnings (Invoke-Moulang 'positive.mcx' $source 'check') @()
Assert-Warnings (Invoke-Moulang 'positive.mcx' $source 'check' -Options @('--tiki=basic')) $basic
Assert-Warnings (Invoke-Moulang 'positive.mcx' $source 'check' -Options @('--tiki=perf')) $perf
Assert-Warnings (Invoke-Moulang 'positive.mcx' $source 'check' -Options @('--tiki=all')) $all
Assert-Warnings (Invoke-Moulang 'positive.mcx' $source 'check' -Options @('--tiki-disable=TI-B001,TI-P002', '--tiki=all')) @($perf[0..1])
Assert-Warnings (Invoke-Moulang 'positive.mcx' $source 'check' -Options @('--tiki=all', '--tiki-disable=TI-B001,TI-P001,TI-P002')) @()

$negative = @'
import "std:text"
struct Box { int value }
main() {
    Box? box = null
    if (box != null) { print(box.value) }
    str accumulator = ""
    foreach (point in "\u{1F642}") { accumulator += point }
    foreach (part in "abc") {
        str local = part
        local += part
        accumulator += ""
        print(local)
    }
    for (step=3 in step++(4)) { accumulator += "x" }
    while (false) { accumulator += "x" }
    while (true) {
        accumulator += "x"
        break
    }
    str ascii = "ascii"
    for (j=0 in j++(text.len(ascii))) { print(ascii[j]) }
    str mixed = "\u{D55C}abc"
    for (k=0 in k++(text.len(mixed))) {
        print(mixed[k])
        mixed = "changed"
    }
    str indexed = "\u{D55C}abc"
    for (n=0 in n++(text.len(indexed))) {
        print(indexed[n])
        n++
    }
    str typeOnly = "\u{D55C}abc"
    for (m=0 in m++(text.len(typeOnly))) { print(type(typeOnly[m])) }
    print(accumulator)
}
'@
Assert-Warnings (Invoke-Moulang 'negative.mcx' $negative 'check' -Options @('--tiki=all')) @()

$nested = @'
main() {
    str result = ""
    foreach (outer in "abc") {
        str local = outer
        for (i=0 in i++(1)) { result += outer }
        foreach (inner in "xy") { local += inner }
        print(local)
    }
    print(result)
}
'@
Assert-Warnings (Invoke-Moulang 'nested.mcx' $nested 'check' -Options @('--tiki=all')) @(
    'nested.mcx:5:31: warning TI-P001',
    'nested.mcx:6:35: warning TI-P001'
)

$smile = [char]::ConvertFromUtf32(0x1F642)
$unused = [char]::ConvertFromUtf32(0xD55C)
$line = "    if (""$smile"" == """") { int $unused = 3 }"
$unicodeSource = @('main() {', $line, '}') -join [char]10
$column = $line.IndexOf($unused, [StringComparison]::Ordinal)
$rich = Invoke-Moulang 'unicode.mcx' $unicodeSource 'check' -Options @('--tiki=basic', '--diagnostic-format=rich')
if ($rich.Code -ne 0 -or $rich.Out -or $rich.Error -notmatch "unicode[.]mcx:2:$($column): warning TI-B001:" -or
    $rich.Error -notmatch '\^' -or $rich.Error -notmatch 'hint:') {
    throw "Unexpected Unicode or rich warning: [$($rich.Error)]."
}
$overlay = Invoke-Moulang 'overlay.mcx' 'invalid source on disk' 'check' -InputBytes $utf8.GetBytes($unicodeSource) -Options @('--stdin', '--library', '--tiki=basic')
Assert-Warnings $overlay @("overlay.mcx:2:$($column): warning TI-B001")
$invalidSource = 'main() { int unused = 1' + [char]10 + 'print(unknown) }'
$invalid = Invoke-Moulang 'invalid.mcx' $invalidSource 'check' -Options @('--tiki=all')
if ($invalid.Code -ne 1 -or $invalid.Error -notmatch 'error ML3004:' -or $invalid.Error -match 'warning TI-') {
    throw "Expected the existing ML error without advisory output: [$($invalid.Error)]."
}

foreach ($options in @(
    '--tiki=unknown', '--tiki-disable=TI-P001', '--tiki=all --tiki-disable=',
    '--tiki=all --tiki-disable=TI-P001,', '--tiki=all --tiki-disable=TI-UNKNOWN',
    '--tiki=all --tiki-disable=TI-B001,TI-B001', '--tiki=basic --tiki=perf'
)) {
    $result = Invoke-Moulang 'positive.mcx' $source 'check' -Options ($options -split ' ')
    if ($result.Code -ne 2 -or $result.Error -notmatch 'Usage:') { throw "Expected usage error for $options." }
}
foreach ($command in 'build', 'run') {
    $result = Invoke-Moulang 'positive.mcx' $source $command -Options @('--tiki=all')
    if ($result.Code -ne 2 -or $result.Error -notmatch 'Usage:') { throw "Expected usage error for $command with Tiki." }
}
Write-Output 'PASS: all Tiki rules, suppression, scope and loop boundaries, Unicode locations, overlays and exit codes.'
