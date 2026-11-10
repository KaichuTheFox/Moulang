param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"

$source = @'
str[][] make() {
    return [
        [],
        ["\u{D55C}", "\u{AE00}"],
        ["end"]
    ]
}
void change(str[][] table) {
    str[] row = table[1]
    row[-1] = "\u{1F600}"
    table[-1] = ["replaced", "row", ""]
}
main() {
    var data = make()
    change(data)
    print(data, type(data), len(data))
    print(data[1], data[-1][-2], type(data[0]))
    data[0].append("entry") {
        print(data[0])
    }
    data.insert(-1, [])
    var last = data.pop()
    print(last, len(data))
    var inferred = [[], ["x"]]
    print(type(inferred), inferred)
    str[][] declared = [[], []]
    declared[0] = inferred[1]
    print(declared)
}
'@
$hangul = [char]::ConvertFromUtf32(0xD55C)
$emoji = [char]::ConvertFromUtf32(0x1F600)
$expected = '[[], ["' + $hangul + '", "' + $emoji + '"], ["replaced", "row", ""]] str[][] 3' + "`n" +
    '["' + $hangul + '", "' + $emoji + '"] row str[]' + "`n" + '["entry"]' + "`n" +
    '["replaced", "row", ""] 3' + "`n" + 'str[][] [[], ["x"]]' + "`n" +
    '[["x"], []]' + "`n"
$r = Invoke-Moulang 'nested.m' $source
if ($r.Code -ne 0 -or $r.Error -or $r.Out -cne $expected) {
    throw "Unexpected nested list output: $($r.Code), [$($r.Out)], [$($r.Error)]."
}

foreach ($case in @(
    @{ Text = 'var x = [["A"], [1]]'; Code = 'ML3018' },
    @{ Text = 'var x = [[], []]'; Code = 'ML3018' },
    @{ Text = 'str[][] x = [["A"], "B"]'; Code = 'ML3018' },
    @{ Text = "str[][] x = [[""A""]]`n    x[0] = ""B"""; Code = 'ML3007' },
    @{ Text = "str[][] x = [[""A""]]`n    x.sort()"; Code = 'ML3043' },
    @{ Text = 'str[][][] x = []'; Code = 'ML2002' },
    @{ Text = 'var x = [[["A"]]]'; Code = 'ML3018' }
)) {
    $r = Invoke-Moulang 'bad-nested.m' "main() {`n    $($case.Text)`n}" 'check'
    if ($r.Code -ne 1 -or $r.Error -notmatch $case.Code) { throw "Expected $($case.Code): $($r.Error)" }
}

foreach ($body in 'print(x[-2][0])', 'print(x[0][1])', 'x[0][-2] = "no"') {
    $r = Invoke-Moulang 'bounds.m' "main() {`n    str[][] x = [[""A""]]`n    $body`n}"
    if ($r.Code -ne 1 -or $r.Out -or $r.Error -notmatch ':3:\d+: Moulang runtime: .*index out of bounds') {
        throw "Expected bounds failure: $($r.Code), [$($r.Out)], [$($r.Error)]."
    }
}
