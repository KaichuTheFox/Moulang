param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot,
    [Parameter(Mandatory)] [string] $Configuration
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$env:MOULANG_ARC_REPORT = '1'

$source = @'
str fromTemporary() {
    return ("abc" + "xyz")[1:5][-2]
}

str fromUtf8Slice(int index) {
    str source = "outside" + "\u{D55C}\u{1F600}\0e\u{301}" + "tail"
    str slice = source[7:-4]
    source = "rebound"
    return slice[index]
}

main() {
    str ascii = ("" + "ab\0cd") * 3
    print(ascii[0], ascii[-1], ascii[2] == "\0")
    str mixed = "a\u{D55C}b\u{1F600}c" + "e\u{301}"
    print(mixed[1], mixed[-4], mixed[-1])
    print(mixed[:5:2][1], mixed[2:3][-1], mixed[::-1][-2])
    str[] words = [ascii, mixed]
    words[0] = mixed[:5:2]
    print(words[0][-1], words.pop()[3], fromTemporary())
    print(fromUtf8Slice(0), fromUtf8Slice(-5), fromUtf8Slice(4), fromUtf8Slice(-1))
    print(fromUtf8Slice(1), fromUtf8Slice(-4), fromUtf8Slice(2) == "\0", fromUtf8Slice(-3) == "\0")
    print("\u{1F600}"[0], "\u{1F600}"[-1], mixed[::-1][0])

    str first = ""
    str second = ""
    print("input") {
        input(first)
        input(second)
    }
    str fileAscii = readText("ascii.txt")
    str fileMixed = readText("mixed.txt")
    print(first[-1], second[1], fileAscii[1], fileMixed[-1])
}
'@
$hangul = [char]::ConvertFromUtf32(0xD55C)
$emoji = [char]::ConvertFromUtf32(0x1F600)
$combining = [char]::ConvertFromUtf32(0x301)
[IO.File]::WriteAllText((Join-Path $work 'ascii.txt'), 'xyz', $utf8)
[IO.File]::WriteAllText((Join-Path $work 'mixed.txt'), "z$emoji", $utf8)
$inputBytes = $utf8.GetBytes(("end{0}q{1}{0}" -f [char]10, $hangul))
$expected = @(
    'a d true', "$hangul $emoji $combining", "b b $hangul",
    "c $emoji x", "$hangul $hangul $combining $combining", "$emoji $emoji true true",
    "$emoji $emoji $combining", 'input', "d $hangul y $emoji", ''
) -join [char]10
$report = if ($Configuration -eq 'Debug') {
    '^Moulang ARC debug: live_objects=0 live_bytes=0 class_roots=0 rooted_objects=0 rooted_bytes=0\r?\n$'
} else { '^$' }
$result = Invoke-Moulang 'ascii-index.m' $source -InputBytes $inputBytes
if ($result.Code -ne 0 -or $result.Out -cne $expected -or $result.Error -notmatch $report) {
    throw "Unexpected string index ($($result.Code)), [$($result.Out)], [$($result.Error)]."
}
foreach ($expression in '""[0]', '""[-1]', '"abc"[3]', '"abc"[-4]', '"abc"[-2147483647 - 1]',
    '"\u{D55C}"[1]', '"\u{1F600}"[-2]', '"\u{D55C}\0\u{1F600}"[2147483647]',
    '"\u{D55C}\0\u{1F600}"[-2147483647 - 1]', '"a\u{D55C}\u{1F600}z"[1:3][2]',
    '"a\u{D55C}\u{1F600}z"[1:3][-3]') {
    $column = 'main() { print('.Length + $expression.LastIndexOf('[') + 1
    $result = Invoke-Moulang 'index-bounds.m' "main() { print($expression) }"
    if ($result.Code -ne 1 -or $result.Out -or
        $result.Error -notmatch "index-bounds[.]m:1:${column}: Moulang runtime: string index out of bounds") {
        throw "Expected a bounds error for $expression ($($result.Code)), [$($result.Out)], [$($result.Error)]."
    }
}
Write-Output 'PASS: ASCII and UTF-8 indexing, bounds, derived strings, input, files and ARC.'
