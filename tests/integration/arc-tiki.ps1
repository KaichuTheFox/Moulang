param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot,
    [Parameter(Mandatory)] [string] $Configuration
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$env:MOULANG_ARC_REPORT = '1'
$report = if ($Configuration -eq 'Debug') {
    '^Moulang ARC debug: live_objects=0 live_bytes=0 class_roots=0 rooted_objects=0 rooted_bytes=0\r?\n$'
} else { '^$' }

function Assert-QuietCheck([string] $Name, [string] $Source, [string[]] $Options) {
    $result = Invoke-Moulang $Name $Source 'check' -Options $Options
    if ($result.Code -ne 0 -or $result.Out -or $result.Error) {
        throw "Expected a quiet check for $Name ($($result.Code)): $($result.Error)"
    }
}

function Assert-Run([string] $Name, [string] $Source, [string] $Expected) {
    $result = Invoke-Moulang $Name $Source
    if ($result.Code -ne 0 -or $result.Out -cne $Expected -or $result.Error -notmatch $report) {
        throw "Unexpected output or ARC report for $Name ($($result.Code)): [$($result.Out)] [$($result.Error)]"
    }
}

$mutations = @(
    @{ List = 'int[]'; Items = '[2, 1]'; Call = 'items.append(3)'; Method = 'append'; Column = 15 },
    @{ List = 'str[]'; Items = '["a", "b"]'; Call = 'second.insert(0, "c")'; Method = 'insert'; Column = 16 },
    @{ List = 'str[]'; Items = '["a", "b"]'; Call = 'second.remove("a")'; Method = 'remove'; Column = 16 },
    @{ List = 'str[][]'; Items = '[["a"], ["b"]]'; Call = 'second.pop()'; Method = 'pop'; Column = 16 }
)
foreach ($mutation in $mutations) {
    $name = $mutation.Method + '.mcx'
    $source = @"
main() {
    $($mutation.List) items = $($mutation.Items)
    var alias = items
    var second = alias
    foreach (_item in items) {
        $($mutation.Call)
    }
}
"@
    $result = Invoke-Moulang $name $source 'check' -Options @('--tiki=deep')
    $at = [regex]::Escape($name) + ":6:$($mutation.Column): warning TI-D001:"
    if ($result.Code -ne 0 -or $result.Out -or $result.Error -notmatch $at -or
        $result.Error -notmatch "'$($mutation.Method)'" -or $result.Error -notmatch 'receiver refers to the list' -or
        $result.Error -notmatch 'Hint:.*after foreach' -or @($result.Error.Trim() -split '\r?\n').Count -ne 1) {
        throw "Expected one TI-D001 with location and guidance for $name`: $($result.Error)"
    }
    $execution = Invoke-Moulang $name $source
    if ($execution.Code -ne 1 -or $execution.Out -or
        $execution.Error -notmatch 'cannot structurally change a list during foreach') {
        throw "The runtime guard did not reject $name`: $($execution.Error)"
    }
    if ($mutation.Method -eq 'insert') {
        Assert-QuietCheck $name $source @()
        Assert-QuietCheck $name $source @('--tiki=deep', '--tiki-disable=TI-D001')
        $basic = Invoke-Moulang $name $source 'check' -Options @('--tiki=basic')
        $all = Invoke-Moulang $name $source 'check' -Options @('--tiki=all', '--diagnostic-format=rich')
        if ($basic.Code -ne 0 -or $basic.Error -match 'TI-D001' -or $all.Code -ne 0 -or
            $all.Error -notmatch $at -or $all.Error -notmatch '\^~~~~~' -or $all.Error -notmatch 'hint:') {
            throw "Unexpected Tiki mode or rich output: $($all.Error)"
        }
    }
}

$capture = @'
void mutate(int[] values) {
    var alias = values
    foreach (_item in alias) {
        values = [1]
        var later = alias
        if (true) { print(later.pop()) }
    }
}
main() { mutate([8]) }
'@
$result = Invoke-Moulang 'captured.mcx' $capture 'check' -Options @('--tiki=deep')
if ($result.Code -ne 0 -or $result.Error -notmatch 'captured[.]mcx:6:33: warning TI-D001:') {
    throw "The saved iterator source should survive rebinding: $($result.Error)"
}
$result = Invoke-Moulang 'captured.mcx' $capture
if ($result.Code -ne 1 -or $result.Error -notmatch 'cannot structurally change a list during foreach') {
    throw "Expected the captured source to stay active: $($result.Error)"
}

$nested = @'
main() {
    int[] values = [1, 2]
    var alias = values
    foreach (item in values) {
        foreach (part in "ab") { alias.append(item) }
    }
}
'@
$result = Invoke-Moulang 'nested.mcx' $nested 'check' -Options @('--tiki=deep')
if ($result.Code -ne 0 -or $result.Error -notmatch 'nested[.]mcx:5:40: warning TI-D001:') {
    throw "The outer list should stay active inside the inner loop: $($result.Error)"
}
$result = Invoke-Moulang 'nested.mcx' $nested
if ($result.Code -ne 1 -or $result.Error -notmatch 'cannot structurally change a list during foreach') {
    throw "Expected the outer iterator's runtime guard: $($result.Error)"
}

$valid = @'
str escapedText() {
    str source = "A" + "\u{D55C}\u{1F642}" + "Z"
    var view = source[1:3]
    source = "changed"
    return view
}
def escapedNumbers() {
    int[] values = [1, 2]
    var alias = values
    var snapshot = values[0:2]
    foreach (value in snapshot) {
        alias.append(value)
    }
    alias[0] = 9
    return snapshot
}
main() {
    str[] shared = [escapedText()]
    str[][] rows = [shared, shared]
    rows[0][0] = escapedText() + "!"
    print(rows[1][0], escapedNumbers()[0])
    int[] items = [1, 2]
    int[] other = [8]
    var alias = items
    var rebound = items
    foreach (item in items) {
        items.sort()
        alias[0] = 1
        other.append(item)
        rebound = other
        rebound.append(item)
        if (false) { alias.append(4) }
        while (false) { alias.append(5) }
        for (zero=1 in zero++(1)) { alias.append(6) }
        print(false && alias.pop() > 0, true ? item : alias.pop())
        continue
        alias.append(7)
    }
    print(items, other)
}
'@
Assert-QuietCheck 'safe.mcx' $valid @('--tiki=all')
$unicode = [string][char]0xD55C + [char]::ConvertFromUtf32(0x1F642)
$expected = "$unicode! 1`nfalse 1`nfalse 2`n[1, 2] [8, 1, 1, 2, 2]`n"
Assert-Run 'safe.mcx' $valid $expected

$loopFacts = @'
void maybe(int[] items, bool useOther) {
    int[] other = []
    var alias = items
    foreach (item in items) {
        if (useOther) { alias = other }
        alias.append(item)
    }
}
void repeat(int[] items) {
    var alias = items
    foreach (item in items) {
        if (item == 1) {
            alias = [3]
            continue
        }
        alias.append(item)
        print(alias)
    }
}
main() {
    maybe([1], true)
    repeat([1, 2])
}
'@
Assert-QuietCheck 'uncertain.mcx' $loopFacts @('--tiki=deep')
Assert-Run 'uncertain.mcx' $loopFacts "[3, 2]`n"

$outside = @'
void mutate(int[] items) { items.append(3) }
main() {
    int[] values = [1, 2]
    foreach (_item in values) { mutate(values) }
}
'@
Assert-QuietCheck 'call-boundary.mcx' $outside @('--tiki=deep')
$result = Invoke-Moulang 'call-boundary.mcx' $outside
if ($result.Code -ne 1 -or $result.Error -notmatch 'cannot structurally change a list during foreach') {
    throw "Expected the runtime to detect mutation across a call: $($result.Error)"
}

$invalidEdges = [ordered]@{
    ML3007 = 'main() { str[][] rows = [["a"]]' + "`n" + 'rows[0] = rows }'
    ML3011 = 'main() { str[] child = ["a"]' + "`n" + 'str[][] rows = [child]' + "`n" + 'child.append(rows) }'
    ML3033 = 'main() { int values[2] = [1, 2]' + "`n" + 'var view = values[0:2]' + "`n" + 'values[0] = 7 }'
}
foreach ($errorCode in $invalidEdges.Keys) {
    $result = Invoke-Moulang "$errorCode.mcx" $invalidEdges[$errorCode] 'check' -Options @('--tiki=all')
    if ($result.Code -ne 1 -or $result.Error -notmatch "error ${errorCode}:" -or $result.Error -match 'warning TI-') {
        throw "Expected $errorCode to take precedence: $($result.Error)"
    }
}

$before = @'
main() {
    str[] pieces = ["", "\u{D55C}", "\0", "\u{1F642}", ""]
    str result = ""
    foreach (piece in pieces) { result += piece }
    WText("before.txt", result)
    print("{}") { format(result) }
}
'@
$after = @'
import "std:text"
main() {
    str[] pieces = ["", "\u{D55C}", "\0", "\u{1F642}", ""]
    str[] collected = []
    foreach (piece in pieces) { collected.append(piece) }
    str result = text.join(collected, "")
    WText("after.txt", result)
    print("{}") { format(result) }
}
'@
$result = Invoke-Moulang 'concat.mcx' $before 'check' -Options @('--tiki=all')
if ($result.Code -ne 0 -or $result.Error -notmatch 'warning TI-P001:.*joining them once' -or $result.Error -match 'TI-D001') {
    throw "Expected the existing accumulation warning: $($result.Error)"
}
Assert-QuietCheck 'join.mcx' $after @('--tiki=all')
$joined = [string][char]0xD55C + [char]0 + [char]::ConvertFromUtf32(0x1F642)
Assert-Run 'concat.mcx' $before "$joined`n"
Assert-Run 'join.mcx' $after "$joined`n"
$expectedBytes = [BitConverter]::ToString($utf8.GetBytes($joined))
foreach ($file in 'before.txt', 'after.txt') {
    if ([BitConverter]::ToString([IO.File]::ReadAllBytes((Join-Path $work $file))) -ne $expectedBytes) {
        throw "Unexpected byte output in $file."
    }
}
Write-Output 'PASS: Deep diagnostics, runtime guard, ARC ownership, managed list edges, and equivalent join output.'
