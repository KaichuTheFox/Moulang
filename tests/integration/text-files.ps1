param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"

$source = @'
main() {
    str path = "\u{D55C}\u{AE00}.txt"
    AText(path, "first\0")
    AText(path, "\u{AC00}" * 25000)
    print(RText(path) == readText(path), RText(path)[5] == "\0", RText(path)[-1])
    WText(path, "replaced")
    AText(path, "\u{1F600}\n")
    print(RText(path) == "replaced\u{1F600}\n")
    AText("empty.txt", "")
    print(readText("empty.txt") == "")
    AText("invalid.txt", "suffix")
}
'@
$invalidPath = Join-Path $work 'invalid.txt'
[IO.File]::WriteAllBytes($invalidPath, [byte[]] @(255, 0))
$r = Invoke-Moulang 'files.m' $source
$hangul = [char]::ConvertFromUtf32(0xAC00)
if ($r.Code -ne 0 -or $r.Error -or $r.Out -cne "true true $hangul`ntrue`ntrue`n") {
    throw "Unexpected text I/O output: $($r.Code), [$($r.Out)], [$($r.Error)]."
}
$filename = [char]::ConvertFromUtf32(0xD55C) + [char]::ConvertFromUtf32(0xAE00) + '.txt'
$contents = [IO.File]::ReadAllText((Join-Path $work $filename), $utf8)
if ($contents -cne ('replaced' + [char]::ConvertFromUtf32(0x1F600) + "`n")) {
    throw 'Appending must preserve existing UTF-8 bytes.'
}
$bytes = [IO.File]::ReadAllBytes($invalidPath)
$expected = [byte[]] (@(255, 0) + $utf8.GetBytes('suffix'))
if ([Convert]::ToBase64String($bytes) -cne [Convert]::ToBase64String($expected)) {
    throw 'Appending must preserve existing bytes without decoding the file.'
}

foreach ($case in @(
    @{ Body = 'print(RText("invalid.txt"))'; Pattern = 'valid UTF-8' },
    @{ Body = 'AText("invalid\0.txt", "x")'; Pattern = 'invalid text file path' },
    @{ Body = 'AText("missing/child.txt", "x")'; Pattern = 'cannot open text file for append' },
    @{ Body = 'AText(".", "x")'; Pattern = 'cannot open text file for append' }
)) {
    $r = Invoke-Moulang 'bad-files.m' "main() { $($case.Body) }"
    if ($r.Code -ne 1 -or $r.Out -or $r.Error -notmatch $case.Pattern) {
        throw "Expected checked text I/O failure: $($r.Code), [$($r.Out)], [$($r.Error)]."
    }
}
foreach ($body in 'RText()', 'WText("file")', 'AText("file", 1)') {
    $r = Invoke-Moulang 'bad-call.m' "main() { $body }" 'check'
    if ($r.Code -ne 1 -or $r.Error -notmatch 'ML3011') { throw "Expected argument diagnostic: $($r.Error)" }
}
