param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot,
    [Parameter(Mandatory)] [string] $Configuration
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$env:MOULANG_ARC_REPORT = '1'

$source = @'
str source() {
    print("source evaluated")
    return "A\u{D55C}" + "\u{1F600}\0e\u{301}Z"
}

str selectSource() {
    print("select source evaluated")
    return "A" + "\u{1F600}Z"
}

str select() {
    foreach (part in selectSource()) {
        if (part == "A") { continue }
        return part
    }
    return "bad"
}

main() {
    str data = source()
    str[] items = []
    foreach (item in data[1:-1]) {
        items.append(item)
        data = "new value"
        item = "local reassignment"
    }
    print(items, len(items), type(items[0]))
    print(select())
    foreach (empty in "") { print("unexpected") }

    int count = 0
    foreach (outer in "ab") {
        foreach (inner in "xy") {
            if (inner == "x") { continue }
            print(outer + inner)
            count++
            break
        }
    }
    print(count)
    int sum = 0
    foreach (number in [1, 2, 3]) { sum += number }
    print(sum)
}
'@
$hangul = [char]::ConvertFromUtf32(0xD55C)
$emoji = [char]::ConvertFromUtf32(0x1F600)
$combining = [char]::ConvertFromUtf32(0x301)
$expected = @(
    'source evaluated',
    ('["' + $hangul + '", "' + $emoji + '", "\0", "e", "' + $combining + '"] 5 str'),
    'select source evaluated', $emoji, 'ay', 'by', '2', '6', ''
) -join [char]10
$result = Invoke-Moulang 'foreach.m' $source
$report = if ($Configuration -eq 'Debug') {
    '^Moulang ARC debug: live_objects=0 live_bytes=0 class_roots=0 rooted_objects=0 rooted_bytes=0\r?\n$'
} else { '^$' }
if ($result.Code -ne 0 -or $result.Out -cne $expected -or $result.Error -notmatch $report) {
    throw "Unexpected string iteration result ($($result.Code)), [$($result.Out)], [$($result.Error)]."
}
foreach ($invalid in '1', 'true', '1.0') {
    $result = Invoke-Moulang 'invalid.m' "main() { foreach (item in $invalid) {} }" 'check'
    if ($result.Code -ne 1 -or $result.Error -notmatch 'ML3027') {
        throw "Expected ML3027 for $invalid ($($result.Error))."
    }
}
Write-Output 'PASS: string code points, ARC lifetimes, loop control, and integer foreach compatibility.'
