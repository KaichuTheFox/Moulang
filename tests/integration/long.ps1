param([Parameter(Mandatory=$true)][string]$Compiler, [Parameter(Mandatory=$true)][string]$TestRoot)
$ErrorActionPreference = 'Stop'
$root = (New-Item -ItemType Directory -Force $TestRoot).FullName
$work = (New-Item -ItemType Directory (Join-Path $root ([guid]::NewGuid().ToString('N')))).FullName
$env:TEMP = $work
$env:TMP = $work
$utf8 = [Text.UTF8Encoding]::new($false)
function Invoke-Test([string]$Program, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    $text = & $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() }
    return @{ Code=$LASTEXITCODE; Text=$text -join "`n" }
}
function Require($Result, [int]$Code, [string]$Text) {
    if ($Result.Code -ne $Code -or $Result.Text -cne $Text) { throw "Expected $Code [$Text], got $($Result.Code) [$($Result.Text)]" }
}
$source = @'
int choose(int x) { return 1 }
int choose(long x) { return 2 }
int nearest(int x) { return 3 }
int nearest(long x) { return 4 }
int width(float x) { return 5 }
int width(double x) { return 6 }
long echo(long x) { return x }
struct Box { long value }
class Item { public long value = 9223372036854775807 }
main() {
    print(2147483648)
    print(2147483647 + 1)
    long maximum = 9223372036854775807
    long minimum = -9223372036854775808
    print(maximum)
    print(minimum)
    print(maximum + 1)
    print(minimum - 1)
    print(-minimum)
    print(minimum / -1)
    print(maximum / 3)
    print(long(1) << 63)
    print(minimum >> 63)
    print(long(3) * long(4294967296))
    print(int(long(2147483647)))
    print(int(long(-2147483648)))
    print(long(4294967295.0))
    print(long(2.5))
    print(long(3.5))
    print(long(18446744073709551616.0))
    print(choose(1))
    print(choose(2147483648))
    print(choose(-2147483648))
    print(nearest(short(1)))
    print(width(short(1)))
    print(echo(2147483648))
    print(Box(maximum).value)
    print((new Item()).value)
    long? optional = minimum
    print(optional ?? 0)
    print(double(long(7)) + 0.5)
}
'@
$expected = @('2147483648','-2147483648','9223372036854775807','-9223372036854775808',
    '-9223372036854775808','9223372036854775807','-9223372036854775808','-9223372036854775808',
    '3074457345618258602','-9223372036854775808','-1','12884901888','2147483647','-2147483648',
    '4294967295','2','4','0','1','2','1','3','5','2147483648','9223372036854775807',
    '9223372036854775807','-9223372036854775808','7.5') -join "`n"
Push-Location $work
try {
    [IO.File]::WriteAllText((Join-Path $work 'long.m'), $source, $utf8)
    Require (Invoke-Test $Compiler @('check','long.m')) 0 ''
    Require (Invoke-Test $Compiler @('run','long.m')) 0 $expected
    foreach ($level in '-O0','-O2','-O3') {
        Require (Invoke-Test $Compiler @('build','long.m','-o','long.exe',$level)) 0 ''
        Require (Invoke-Test (Join-Path $work 'long.exe') @()) 0 $expected
    }
    [IO.File]::WriteAllText((Join-Path $work 'library.m'),
        'int imported(int x) { return 1 }' + "`n" + 'int imported(long x) { return 2 }' + "`n" +
        'private int imported(bool x) { return 3 }', $utf8)
    [IO.File]::WriteAllText((Join-Path $work 'imports.m'),
        'import "library.m"' + "`n" + 'main() { print(imported(1))' + "`n" + 'print(imported(2147483648)) }', $utf8)
    Require (Invoke-Test $Compiler @('run','imports.m')) 0 "1`n2"
    [IO.File]::WriteAllText((Join-Path $work 'hidden.m'),
        'import "library.m"' + "`n" + 'main() { print(imported(true)) }', $utf8)
    $hidden = Invoke-Test $Compiler @('check','hidden.m')
    if ($hidden.Code -ne 1 -or $hidden.Text -notmatch 'ML3011') { throw 'Private overload leaked across a module boundary.' }
    [IO.File]::WriteAllText((Join-Path $work 'other.m'), 'int imported(str x) { return 4 }', $utf8)
    [IO.File]::WriteAllText((Join-Path $work 'conflict.m'),
        'import "library.m"' + "`n" + 'import "other.m"' + "`nmain() {}", $utf8)
    $conflict = Invoke-Test $Compiler @('check','conflict.m')
    if ($conflict.Code -ne 1 -or $conflict.Text -notmatch 'ML3017') { throw 'Cross-module public overload groups merged.' }
    $failures = @(
        @('main() { print(9223372036854775808) }','ML3003'),
        @('main() { print(-9223372036854775809) }','ML3003'),
        @('main() { int n = long(1) }','ML3007'),
        @('main() { double n = long(1) }','ML3007'),
        @('main() { print(long(1) + 0.5) }','ML3005'),
        @('main() { print(true ? long(1) : 0.5) }','ML3026'),
        @('main() { print([2147483648]) }','ML3018'),
        @('int f(int x) { return 1 }' + "`n" + 'long f(int x) { return 2 }' + "`nmain() {}",'ML3017'),
        @('int f(int x, long y) { return 1 }' + "`n" + 'int f(long x, int y) { return 2 }' + "`nmain() { print(f(1,1)) }",'Ambiguous'),
        @('int f(int x) { return 1 }' + "`n" + 'int f(long x) { return 2 }' + "`nmain() { print(f(true)) }",'No overload')
    )
    foreach ($failure in $failures) {
        [IO.File]::WriteAllText((Join-Path $work 'invalid.m'), $failure[0], $utf8)
        $result = Invoke-Test $Compiler @('check','invalid.m')
        if ($result.Code -ne 1 -or $result.Text -notmatch ('invalid.m:\d+:\d+: error .*' + $failure[1])) {
            throw "Unexpected compile diagnostic: $($result.Text)"
        }
    }
    foreach ($expression in 'int(2147483648)','int(-2147483649)','long(1) / long(0)',
        'long(1) << 64','long(1) << -1','long(1) << 4294967296','1 << 4294967296','long(1.0 / 0.0)') {
        [IO.File]::WriteAllText((Join-Path $work 'runtime.m'), 'main() { print(' + $expression + ') }', $utf8)
        foreach ($level in '-O0','-O2','-O3') {
            Require (Invoke-Test $Compiler @('build','runtime.m','-o','runtime.exe',$level)) 0 ''
            $result = Invoke-Test (Join-Path $work 'runtime.exe') @()
            if ($result.Code -ne 1 -or $result.Text -notmatch 'runtime.m:1:\d+: Moulang runtime:') {
                throw "Expected runtime range/site error for $expression, got $($result.Text)"
            }
        }
    }
    Write-Output 'PASS: long boundaries, full-width ABI, checked narrowing, precision rules, wrapping and overload ranking.'
} finally { Pop-Location }
