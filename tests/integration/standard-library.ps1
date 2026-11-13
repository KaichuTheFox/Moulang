param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot,
    [Parameter(Mandatory)] [string] $Configuration
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$env:MOULANG_ARC_REPORT = '1'

[IO.File]::WriteAllText((Join-Path $work 'local.m'), @'
import "std:math" as nestedMath
int sqrt(int value) { return value + 1 }
double root(double value) { return nestedMath.sqrt(value) }
'@, $utf8)

$source = @'
import "local.m"
import "std:math"
import "std:text" as strings
import "std:time"
import "std:random"

str[] pieces() {
    str prefix = "\u{D55C}" + "\u{1F642}"
    return strings.split(prefix + "::x::::", "::")
}

int shadow() {
    int[] math = []
    math.append(17)
    return math.pop()
}

main() {
    print(sqrt(9), math.sqrt(81), root(49.0), shadow())
    print(math.abs(-7), math.abs(-2147483649), math.abs(-3.5))
    print(math.min(short(5), 2), math.min(7, 2147483648), math.min(1, 2.5))
    print(math.max(-4, 5), math.max(2147483648, 2), math.max(-0.5, -1.0))
    print(math.pow(2, 10), math.pow(0, 0), 1.0 / math.min(-0.0, 0.0) < 0.0, 1.0 / math.abs(-0.0) > 0.0)

    str mixed = "X\u{D55C}\u{1F642}\0Z"
    print(strings.len(mixed), strings.len("e\u{301}"), strings.find(mixed, "\u{1F642}"),
        strings.find(mixed, "\0"), strings.find(mixed, "missing"), strings.find(mixed, ""))
    print(strings.substring(mixed, -4, -1) == "\u{D55C}\u{1F642}\0", strings.substring("abc", -99, 99) == "abc")
    print(strings.lowerAscii(mixed) == "x\u{D55C}\u{1F642}\0z", strings.upperAscii("a\u{00E9}\0") == "A\u{00E9}\0")
    print(strings.replace("aaaaa", "aa", "x"), strings.replace(mixed, "\0", "!") == "X\u{D55C}\u{1F642}!Z")
    print(strings.replace("abc", "abc", "") == "", strings.replace("ABC", "x", "z") == "ABC")
    var parts = pieces()
    print(len(parts), parts[0] == "\u{D55C}\u{1F642}", parts[1], parts[-1] == "")
    print(strings.join(parts, ":") == "\u{D55C}\u{1F642}:x::", strings.join([], ":") == "",
        strings.join(strings.split("::a::::", "::"), "::") == "::a::::",
        len(strings.split("", "::")) == 1, strings.join(["a", "b"], "\0") == "a\0b")

    print(time.formatUtcDate(0), time.formatUtcDate(-1), time.formatUtcDate(951782400000))
    long start = time.monotonicNanos()
    long finish = time.monotonicNanos()
    print(type(time.unixMillis()), time.unixMillis() > 0, start >= 0, finish >= start)

    print(random.nextInt(3, 3), random.nextDouble() == 0.8833108082136426)
    print(random.nextInt(-2147483648, 2147483647), random.nextInt(1, 6))
    random.seed(-17)
    var sample = random.nextDouble()
    random.seed(-17)
    print(random.nextDouble() == sample, sample >= 0.0, sample < 1.0)
    print("value {}") { format(math.abs(-7)) }
}
'@
$expected = @(
    '10 9 7 17', '7 2147483649 3.5', '2 7 1', '5 2147483648 -0.5', '1024 1 true true',
    '5 2 2 3 -1 0', 'true true', 'true true', 'xxa true', 'true true', '4 true x true',
    'true true true true true', '1970-01-01 1969-12-31 2000-02-29', 'long true true true',
    '3 true', '565798388 2', 'true true true', 'value 7', ''
) -join [char]10
$report = if ($Configuration -eq 'Debug') {
    '^Moulang ARC debug: live_objects=0 live_bytes=0 class_roots=0 rooted_objects=0 rooted_bytes=0\r?\n$'
} else { '^$' }
$result = Invoke-Moulang 'standard.m' $source
if ($result.Code -ne 0 -or $result.Out -cne $expected -or $result.Error -notmatch $report) {
    throw "Unexpected standard library result ($($result.Code)), [$($result.Out)], [$($result.Error)]."
}

foreach ($case in @(
    @{ Imports = @('import "std:missing"'); Body = ''; Code = 'ML3044' },
    @{ Imports = @('import "std:math"', 'import "std:text" as math'); Body = ''; Code = 'ML3044' },
    @{ Imports = @('import "std:math" as len'); Body = ''; Code = 'ML3044' },
    @{ Imports = @('import "std:math"', 'int math() { return 1 }'); Body = ''; Code = 'ML3044' },
    @{ Imports = @('import "std:text"', 'struct text { int value }'); Body = ''; Code = 'ML3044' },
    @{ Imports = @('import "std:math"'); Body = 'math.missing(1)'; Code = 'ML3045' },
    @{ Imports = @('import "std:math"'); Body = 'math?.sqrt(1)'; Code = 'ML3045' },
    @{ Imports = @('import "std:math"'); Body = 'math.pow(long(2), 3)'; Code = 'ML3011' },
    @{ Imports = @('import "std:math"'); Body = 'math.sqrt()'; Code = 'ML3011' },
    @{ Imports = @('import "std:random"'); Body = 'var value = random.seed(1)'; Code = 'ML3012' },
    @{ Imports = @('import "local.m"'); Body = 'nestedMath.sqrt(4)'; Code = 'ML3004' }
)) {
    $inputSource = ($case.Imports + "main() { $($case.Body) }") -join [char]10
    $result = Invoke-Moulang 'bad-import.m' $inputSource 'check'
    if ($result.Code -ne 1 -or $result.Error -notmatch "bad-import[.]m:\d+:\d+: error $($case.Code):") {
        throw "Expected $($case.Code) for [$inputSource], got [$($result.Error)]."
    }
}

foreach ($case in @(
    @{ Module = 'math'; Call = 'math.abs(-2147483648)'; Message = 'outside the integer range' },
    @{ Module = 'math'; Call = 'math.abs(-9223372036854775808)'; Message = 'outside the integer range' },
    @{ Module = 'math'; Call = 'math.sqrt(-1.0)'; Message = 'nonnegative argument' },
    @{ Module = 'math'; Call = 'math.max(0.0 / 0.0, 1.0)'; Message = 'finite arguments' },
    @{ Module = 'math'; Call = 'math.pow(-1.0, 0.5)'; Message = 'outside its finite domain' },
    @{ Module = 'math'; Call = 'math.pow(2, 1024)'; Message = 'outside its finite domain' },
    @{ Module = 'text'; Call = 'text.replace("a", "", "b")'; Message = 'nonempty needle' },
    @{ Module = 'text'; Call = 'text.split("a", "")'; Message = 'nonempty separator' },
    @{ Module = 'time'; Call = 'time.formatUtcDate(-9223372036854775808)'; Message = 'UTC year from 0001 through 9999' },
    @{ Module = 'time'; Call = 'time.formatUtcDate(9223372036854775807)'; Message = 'UTC year from 0001 through 9999' },
    @{ Module = 'random'; Call = 'random.nextInt(4, 3)'; Message = 'inverted bounds' }
)) {
    $import = 'import "std:' + $case.Module + '"'
    $inputSource = @($import, "main() { $($case.Call) }") -join [char]10
    $result = Invoke-Moulang 'runtime-error.m' $inputSource
    if ($result.Code -ne 1 -or $result.Out -or
        $result.Error -notmatch "runtime-error[.]m:2:\d+: Moulang runtime: .*$($case.Message)") {
        throw "Expected a runtime error for $($case.Call), got [$($result.Error)]."
    }
}
Write-Output 'PASS: standard module scope, math, Unicode text and ARC, UTC and monotonic time, and deterministic random.'
