param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"

$hangul = [char]::ConvertFromUtf32(0xAC00)
$emoji = [char]::ConvertFromUtf32(0x1F600)
$source = @'
str @NAME@1(str x) {
    print("evaluated", x)
    return x
}

main() {
    str @NAME@ = "@NAME@\u{1F600}\0Z" /* @NAME@
    */
    float f = 2.5
    print(10, f, @NAME@[0], @NAME@[1], @NAME@[-1])
    print(@NAME@[:2], @NAME@[1:-1], @NAME@[-1:], @NAME@[::2])
    print(@NAME@[::-1])
    print(@NAME@[5:1], @NAME@[-999:999:2], ""[::-1])
    print("/* literal */" + "// literal", "AB" * 2, "X" * 0)
    print(type(@NAME@1("skip")), type(f), type(2147483648), type([1, 2]))
    print(@NAME@1("left"), @NAME@1("right"))
    // An ordinary comment
}
'@
$r = Invoke-Moulang 'text.m' ($source.Replace('@NAME@', $hangul))
$nul = [char]0
$expected = "10 2.5 $hangul $emoji Z`n" +
    "$hangul$emoji $emoji$nul Z $hangul$nul`n" +
    "Z$nul$emoji$hangul`n $hangul$nul `n" +
    "/* literal */// literal ABAB `nstr float long int[]`n" +
    "evaluated left`nevaluated right`nleft right`n"
if ($r.Code -ne 0 -or $r.Error -or $r.Out -cne $expected) {
    throw "Unexpected text output or diagnostic: $($r.Code), [$($r.Out)], [$($r.Error)]."
}

$compileErrors = @(
    @{ Body = 'main() { string old = "" }'; Pattern = "ML2002.*Use 'str'" },
    @{ Body = 'main() { print(type()) }'; Pattern = 'ML3011' },
    @{ Body = 'main() { print(type(1, 2)) }'; Pattern = 'ML3011' },
    @{ Body = "main() {`n    /* unfinished"; Pattern = ':2:5: error ML1006' },
    @{ Body = 'main() { print([1, 2][:1]) }'; Pattern = 'ML3018' }
)
foreach ($case in $compileErrors) {
    $r = Invoke-Moulang 'invalid.m' $case.Body 'check'
    if ($r.Code -ne 1 -or $r.Error -notmatch $case.Pattern) { throw "Missing diagnostic [$($case.Pattern)]: $($r.Error)" }
}

$runtimeErrors = @(
    @{ Expr = '"X"[1]'; Pattern = 'index.*bounds' },
    @{ Expr = '"X"[-2]'; Pattern = 'index.*bounds' },
    @{ Expr = '"X"[::0]'; Pattern = 'step.*zero' },
    @{ Expr = '"X" * -1'; Pattern = 'repetition.*negative' },
    @{ Expr = '"XX" * 2147483647'; Pattern = 'length.*overflow|too long|length.*limit' }
)
foreach ($case in $runtimeErrors) {
    $r = Invoke-Moulang 'invalid.m' "main() { print($($case.Expr)) }"
    if ($r.Code -ne 1 -or $r.Out -or $r.Error -notmatch $case.Pattern) {
        throw "Expected runtime error [$($case.Pattern)]: $($r.Code), [$($r.Out)], [$($r.Error)]."
    }
}
