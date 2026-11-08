param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $Hello,
    [Parameter(Mandatory)] [string] $TestRoot
)

$ErrorActionPreference = 'Stop'
$utf8 = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $utf8
$workspace = Join-Path $TestRoot ([guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Force -Path $workspace

function Invoke-Native([string] $Program, [string[]] $Arguments) {
    $ErrorActionPreference = 'Continue'
    $lines = & $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() }
    return @{ Code = $LASTEXITCODE; Text = $lines -join "`n" }
}

function Check($Result, [int] $Code, [string] $Expected) {
    if ($Result.Code -ne $Code -or $Result.Text -cne $Expected) {
        throw "Expected exit $Code and [$Expected], got $($Result.Code) and [$($Result.Text)]."
    }
}

function Check-RuntimeError([string] $Name, [string] $Message, [string] $File = $Name,
    [string] $Coordinates = '\d+:\d+') {
    $result = Invoke-Native $Compiler @('run', $Name)
    $expected = [regex]::Escape($File) + ':' + $Coordinates + ': Moulang runtime: ' + [regex]::Escape($Message) + '$'
    if ($result.Code -ne 1 -or $result.Text -notmatch $expected) {
        throw "Expected runtime diagnostic in $File, got $($result.Code) and [$($result.Text)]."
    }
}

function Check-Error([string] $Name, [string] $Source, [string] $Diagnostic) {
    [IO.File]::WriteAllText((Join-Path $workspace $Name), $Source, $utf8)
    $result = Invoke-Native $Compiler @('build', $Name)
    if ($result.Code -ne 1 -or $result.Text -notmatch $Diagnostic -or
        (Test-Path ([IO.Path]::ChangeExtension($Name, '.exe')))) {
        throw "Unexpected diagnostic for ${Name}: $($result.Code), $($result.Text)"
    }
}

Push-Location $workspace
try {
    $env:TEMP = $workspace
    $env:TMP = $workspace
    Check (Invoke-Native $Compiler @('--version')) 0 'Moulang 0.2.0'
    $help = Invoke-Native $Compiler @('--help')
    if ($help.Code -ne 0 -or $help.Text -notmatch '^Usage:') {
        throw "Expected CLI help, got $($help.Text)"
    }
    Copy-Item -LiteralPath $Hello -Destination hello.m
    Check (Invoke-Native $Compiler @('run', 'hello.m')) 0 'Hello Moulang!'
    Check (Invoke-Native $Compiler @('build', 'hello.m')) 0 ''
    Check (Invoke-Native '.\hello.exe' @()) 0 'Hello Moulang!'

    $expressions = @'
main() {
    int value = 2 + 3 * 4
    var count = value
    bool ready = count == 14 && !false
    string label = "initial"
    label = "updated"
    print(label)
    print(ready)
    count = (count + 1) / -2
    print(count)
    print(-2147483648)
    print(2147483647 + 1)
    print(false && 1 / 0 == 0)
    print(true || 1 / 0 == 0)
    print((false || true) && (false || true))
}
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'expressions.m'), $expressions, $utf8)
    $expressionOutput = @('updated', 'true', '-7', '-2147483648', '-2147483648', 'false', 'true', 'true') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'expressions.m')) 0 $expressionOutput
    Check (Invoke-Native $Compiler @('build', 'expressions.m')) 0 ''
    Check (Invoke-Native '.\expressions.exe' @()) 0 $expressionOutput
    [IO.File]::WriteAllText((Join-Path $workspace 'divide.m'), 'main() { print(1 / 0) }', $utf8)
    Check-RuntimeError 'divide.m' 'division by zero.'

    $control = @'
main() {
    print(factorial(5))
    print(forward(5))
    print(echo("ok"))
    greet()
    int count = 0
    for (i=0 in i++(limit())) {
        if (i == 1) { continue }
        elif (i == 4) { break }
        else {
            int step = i
            while (step > 0) {
                step = step - 1
                count = count + 1
            }
        }
    }
    for (j=5 in j++(5)) { print("unreachable") }
    print(count)
    while (count > 3) {
        count = count - 1
        if (count == 4) { continue }
        print(count)
    }
    return
    print("unreachable")
}

int factorial(int n) {
    if (n < 2) { return 1 }
    return n * factorial(n - 1)
}

def forward(int n) { return doubleValue(n) }
def doubleValue(int n) { return n * 2 }
string echo(string value) { return value }
void greet() { print("hello") }
int limit() {
    print("limit")
    return 6
}
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'control.m'), $control, $utf8)
    $controlOutput = @('120', '10', 'ok', 'hello', 'limit', '5', '3') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'control.m')) 0 $controlOutput
    Check (Invoke-Native $Compiler @('build', 'control.m')) 0 ''
    Check (Invoke-Native '.\control.exe' @()) 0 $controlOutput

    $collections = @'
main() {
    int numbers[] = [10, 20, 30]
    var first = append(numbers, 40)
    var second = append(numbers, 99)
    var larger = append(first, 50)
    print(first[3])
    print(second[3])
    print(larger[4])
    var empty = []
    empty = append(empty, 7)
    print(empty[0])
    int value = 2
    value++
    value *= 5
    value -= 3
    value /= 2
    value--
    value += 10
    print(value)
    print("equal" == echo("equal"))
    print("a" != "aa")
}
string echo(string text) { return text }
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'collections.m'), $collections, $utf8)
    $collectionOutput = @('40', '99', '50', '7', '15', 'true', 'true') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'collections.m')) 0 $collectionOutput
    Check (Invoke-Native $Compiler @('build', 'collections.m')) 0 ''
    Check (Invoke-Native '.\collections.exe' @()) 0 $collectionOutput
    [IO.File]::WriteAllText((Join-Path $workspace 'bounds.m'), 'main() { print([1][-1]) }', $utf8)
    Check-RuntimeError 'bounds.m' 'slice index out of bounds.'
    [IO.File]::WriteAllText((Join-Path $workspace 'bounds.m'), 'main() { print([1][1]) }', $utf8)
    Check-RuntimeError 'bounds.m' 'slice index out of bounds.'

    $nulls = @'
main() {
    string? nickname = null
    print(nickname ?? "Unknown")
    nickname = "Moulang"
    if (nickname != null) { print(nickname) }
    if (null == nickname) { print("unreachable") }
    else { print(nickname) }
    int? n = maybe(true, 5)
    print(n ?? (1 / 0))
    print(maybe(false, 6) ?? 9)
    print(isNull(null))
    print(isNull(false))
    int? missing = null
    print(missing ?? missing ?? 17)
    if (n != null && n > 3) { print(n) }
    while (n != null) {
        print(n)
        n = null
    }
}
int? maybe(bool present, int n) {
    if (present) { return n }
    else { return null }
}
bool isNull(bool? value) { return value == null }
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'nullable.m'), $nulls, $utf8)
    $nullOutput = @('Unknown', 'Moulang', 'Moulang', '5', '9', 'true', 'false', '17', '5', '5') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'nullable.m')) 0 $nullOutput
    Check (Invoke-Native $Compiler @('build', 'nullable.m')) 0 ''
    Check (Invoke-Native '.\nullable.exe' @()) 0 $nullOutput

    $numbers = @'
main() {
    print(char(-1))
    print(char(260))
    print(short(65535))
    print(short(-32769))
    long count = 2147483647
    count++
    print(count)
    print(long(4294967295.0))
    print(int(4294967297.0))
    print(int(-4294967297.0))
    print(int(2.5))
    print(int(3.5))
    print(int(-2.5))
    print(int(-3.5))
    print(int(-0.5))
    print(int(0.5000000000000001))
    print(int(9007199254740994.0))
    print(int(1e300))
    float f = short(16384)
    f++
    print(f)
    print(float(0.1))
    print(double(float(16777217)))
    print(float(0.25) + 16777217)
    print(exact(16777217) == 16777217.0)
    print(char(200) > char(100))
    print(short(-1) < char(200))
    double? a = char(9)
    print(a + 0.5)
    a = null
    print(a ?? short(7))
    print(0.0 / 0.0 != 0.0)
}
double exact(double value) { return value }
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'numbers.m'), $numbers, $utf8)
    $numericOutput = @('255', '4', '-1', '32767', '2147483648', '4294967295', '1', '-1', '2', '4', '-2', '-4',
        '0', '1', '2', '0', '16385', '0.1', '16777216', '16777217.25', 'true', 'true', 'true', '9.5', '7',
        'true') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'numbers.m')) 0 $numericOutput
    Check (Invoke-Native $Compiler @('build', 'numbers.m')) 0 ''
    Check (Invoke-Native '.\numbers.exe' @()) 0 $numericOutput
    foreach ($nonFinite in '1.0 / 0.0', '0.0 / 0.0') {
        [IO.File]::WriteAllText((Join-Path $workspace 'nonfinite.m'),
            "main() { print(int($nonFinite)) }", $utf8)
        Check-RuntimeError 'nonfinite.m' 'cannot convert a non-finite real to an integer.'
    }
    [IO.File]::WriteAllText((Join-Path $workspace 'float-range.m'), 'main() { print(float(1e300)) }', $utf8)
    Check-RuntimeError 'float-range.m' 'real value is outside the float range.'

    $structures = @'
struct LabelledPosition {
    string label
    Position position
}

struct Position {
    float x
    float y
}

struct Sample {
    int? value
}

main() {
    Position pos = Position(10, 20)
    var copy = pos
    pos = Position(float(3), float(4))
    print(copy.x)
    print(pos.y)
    var item = LabelledPosition("home", translate(copy))
    print(item.label)
    print(item.position.x)
    print(item.position.y)
    print(makeSample().value ?? 7)
    print(Position(float(report(1)), float(report(2))).y)
}

Position translate(Position input) {
    return Position(input.x + float(0.5), input.y + float(2))
}
def makeSample() { return Sample(null) }
int report(int number) {
    print(number)
    return number
}
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'structs.m'), $structures, $utf8)
    $structureOutput = @('10', '4', 'home', '10.5', '22', '7', '1', '2', '2') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'structs.m')) 0 $structureOutput
    Check (Invoke-Native $Compiler @('build', 'structs.m')) 0 ''
    Check (Invoke-Native '.\structs.exe' @()) 0 $structureOutput

    $features = @'
struct Point { int x }
struct Box { Point point }
struct OptionalPoint { Point? value }
main() {
    Box box = Box(Point(3))
    Box original = box
    box.point.x = 9
    print(original.point.x)
    print(box.point.x)
    print(2 | 1 << 2)
    print(15 & 6 ^ 2)
    print(~char(0))
    print(char(128) >> 7)
    print(-4 >> 1)
    print(true ? 42 : (1 / 0))
    print(false ? "never" : true ? "nested" : "other")
    print(true ? short(1) : 0.5)
    print((false ? Point(0) : Point(17)).x)
    Point? maybe = null
    print(maybe?.x ?? -1)
    maybe = Point(8)
    print(maybe?.x ?? -1)
    OptionalPoint? optional = OptionalPoint(maybe)
    print(optional?.value?.x ?? -1)
    optional = OptionalPoint(null)
    print(optional?.value?.x ?? -1)
    if (maybe != null) { maybe.x = 11 }
    print(maybe?.x ?? -1)

    var numbers = [10, 20, 30, 40, 50]
    var view = numbers[1:4]
    numbers = append(numbers, 60)
    print(view[1:2][0])
    foreach (value in view) {
        if (value == 30) { continue }
        print(value)
    }
    foreach (value in []) { print("unreachable") }
    foreach (value in [1, 2, 3]) {
        if (value == 2) { break }
        print(value)
    }
}
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'features.m'), $features, $utf8)
    $featureOutput = @('3', '9', '6', '4', '255', '1', '-2', '42', 'nested', '1', '17',
        '-1', '8', '8', '-1', '11', '30', '20', '40', '1') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'features.m')) 0 $featureOutput
    Check (Invoke-Native $Compiler @('build', 'features.m')) 0 ''
    Check (Invoke-Native '.\features.exe' @()) 0 $featureOutput
    $arrays = @'
main() {
    int numbers[3] = [10, 20, 30]
    var copy = numbers
    copy[0] = 70
    print(numbers[0])
    print(copy[0])
    if (true) {
        var view = numbers[1:3]
        var inner = view[0:1]
        print(inner[0])
        copy[1] = 80
        print(copy[1])
    }
    numbers[1] = 50
    foreach (item in numbers) {
        print(item)
    }
    int empty[0] = []
    foreach (item in empty) { print("unreachable") }
}
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'arrays.m'), $arrays, $utf8)
    $arrayOutput = @('10', '70', '20', '80', '10', '50', '30') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'arrays.m')) 0 $arrayOutput
    Check (Invoke-Native $Compiler @('build', 'arrays.m', '-O2')) 0 ''
    Check (Invoke-Native '.\arrays.exe' @()) 0 $arrayOutput
    [IO.File]::WriteAllText((Join-Path $workspace 'fixed-bounds.m'),
        "main() { int a[1] = [1]`na[1] = 2 }", $utf8)
    Check-RuntimeError 'fixed-bounds.m' 'fixed array index out of bounds.'
    $classes = @'
class Counter {
    int count
    public Counter(int start) { count = start }
    public void increase() { count = count + 1 }
    public int value() { return count }
}

class Player {
    private Counter points = new Counter(10)
    public string name = "Player"
    public int score() { return points.value() }
    public void play() { points.increase() }
}

class Node {
    public Node? next = null
}

class Base {
    public int value = 1
    public Base() { value = 2 }
    public int baseValue() { return value }
}

class Child : Base {
    public int bonus = 4
    public int total() { return value + bonus }
}

class Grandchild : Child {
    public int extra = 6
}

Counter factory() { return new Counter(4) }

main() {
    Counter a = factory()
    Counter b = a
    b.increase()
    print(a.value())
    Player p = new Player()
    p.play()
    print(p.score())
    Player? nullable = p
    print(nullable?.name ?? "missing")
    nullable?.play()
    print(nullable?.score() ?? 0)
    nullable = null
    print(nullable?.name ?? "missing")
    nullable?.play()
    print(nullable?.score() ?? -1)
    Node link = new Node()
    link.next = link
    print(link.next != null)
    Grandchild object = new Grandchild()
    Base base = object
    print(object.total())
    object.value = 8
    print(base.baseValue())
    print(object.extra)
    Base? maybe = object
    print(maybe?.value ?? 0)
}
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'classes.m'), $classes, $utf8)
    $classOutput = @('5', '11', 'Player', '12', 'missing', '-1', 'true', '6', '8', '6', '8') -join "`n"
    Check (Invoke-Native $Compiler @('run', 'classes.m')) 0 $classOutput
    Check (Invoke-Native $Compiler @('build', 'classes.m', '-O3')) 0 ''
    Check (Invoke-Native '.\classes.exe' @()) 0 $classOutput
    foreach ($level in '-O0', '-O2', '-O3') {
        Check (Invoke-Native $Compiler @('build', 'features.m', $level, '-o', 'optimized.exe')) 0 ''
        Check (Invoke-Native '.\optimized.exe' @()) 0 $featureOutput
    }
    foreach ($shift in '1 << 32', '1 >> -1', 'char(1) << 8') {
        [IO.File]::WriteAllText((Join-Path $workspace 'shift.m'), "main() { print($shift) }", $utf8)
        Check-RuntimeError 'shift.m' 'invalid bit shift count.'
    }
    foreach ($range in '[-1:0]', '[1:0]', '[0:2]') {
        [IO.File]::WriteAllText((Join-Path $workspace 'range.m'), "main() { var view = [1]$range }", $utf8)
        Check-RuntimeError 'range.m' 'slice range out of bounds.'
    }

    $moduleDir = New-Item -ItemType Directory -Path 'modules'
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'common.m'),
        "struct Data { int value }`nData makeData() { return Data(42) }", $utf8)
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'helper.m'),
        "import `"common.m`"`nint answer(Data data) { return data.value }", $utf8)
    $entryText = "import `"modules/helper.m`"`nimport `"modules/common.m`"`nmain() { print(answer(makeData())) }"
    [IO.File]::WriteAllText((Join-Path $workspace 'modules.m'), $entryText, $utf8)
    Check (Invoke-Native $Compiler @('run', 'modules.m')) 0 '42'
    Check (Invoke-Native $Compiler @('check', 'modules.m')) 0 ''
    Check (Invoke-Native $Compiler @('check', 'modules/common.m', '--library')) 0 ''
    $overlay = & {
        $ErrorActionPreference = 'Continue'
        'int overlay() { return true }' | & $Compiler check 'modules/common.m' --stdin --library 2>&1 |
            ForEach-Object { $_.ToString() }
    }
    if ($LASTEXITCODE -ne 1 -or ($overlay -join "`n") -notmatch 'common\.m:1:\d+: error ML3013') {
        throw "Expected diagnostic in unsaved source, got $overlay"
    }
    if (Test-Path -LiteralPath modules.exe) { throw 'Check must not create an executable.' }
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'first.m'),
        "private struct Hidden { int value }`nprivate int helper() { return Hidden(10).value }`npublic int first() { return helper() }", $utf8)
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'second.m'),
        "private struct Hidden { int value }`nprivate int helper() { return Hidden(20).value }`npublic int second() { return helper() }", $utf8)
    $visibility = "import `"modules/first.m`"`nimport `"modules/second.m`"`n"
    [IO.File]::WriteAllText((Join-Path $workspace 'visibility.m'),
        ($visibility + 'main() { print(first() + second()) }'), $utf8)
    Check (Invoke-Native $Compiler @('run', 'visibility.m')) 0 '30'
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'objects.m'),
        "public class Item { public int value = 12 }`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $workspace 'import-class.m'),
        "import `"modules/objects.m`"`nmain() { Item item = new Item()`nprint(item.value) }", $utf8)
    Check (Invoke-Native $Compiler @('run', 'import-class.m')) 0 '12'
    Check-Error 'private-call.m' ($visibility + 'main() { print(helper()) }') 'ML3010'
    Check-Error 'private-type.m' ($visibility + 'main() { Hidden value = Hidden(1) }') 'ML3021'
    Check-Error 'private-export.m' "private struct Hidden {}`npublic Hidden make() { return Hidden() }`nmain() {}" 'ML3031'
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'sibling.m'), 'int sibling() { return first() }', $utf8)
    Check-Error 'sibling.m' ($visibility + "import `"modules/sibling.m`"`nmain() {}") 'ML3010'
    $importedDivide = 'int importedDivide() { return 1 / 0 }'
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'divide.m'), $importedDivide, $utf8)
    [IO.File]::WriteAllText((Join-Path $workspace 'imported-runtime.m'),
        "import `"modules/divide.m`"`nmain() { print(importedDivide()) }", $utf8)
    Check-RuntimeError 'imported-runtime.m' 'division by zero.' 'divide.m' "1:$($importedDivide.IndexOf('/') + 1)"
    $overwriteImport = Invoke-Native $Compiler @('build', 'modules.m', '-o', 'modules/common.m')
    if ($overwriteImport.Code -ne 1 -or $overwriteImport.Text -notmatch 'overwrite the source file') {
        throw "Expected imported source overwrite diagnostic, got $($overwriteImport.Text)"
    }
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'broken.m'), 'int bad() { return true }', $utf8)
    Check-Error 'bad-import.m' "import `"modules/broken.m`"`nmain() {}" 'broken\.m:1:\d+: error ML3013'
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'cycle.m'), 'import "../cycle-entry.m"', $utf8)
    Check-Error 'cycle-entry.m' "import `"modules/cycle.m`"`nmain() {}" 'cycle\.m:1:\d+: error ML3029'
    Check-Error 'missing-import.m' "import `"missing.m`"`nmain() {}" 'ML3028'
    [IO.File]::WriteAllText((Join-Path $moduleDir.FullName 'another-main.m'), 'main() {}', $utf8)
    Check-Error 'import-main.m' "import `"modules/another-main.m`"`nmain() {}" 'ML3030'

    $sampleText = [string][char]0xBA38 + [char]0xB7AD
    $textName = "$sampleText text.txt"
    $fileProgram = @"
main() {
    writeText("$textName", "old")
    writeText("$textName", "$sampleText")
    print(readText("$textName"))
}
"@
    [IO.File]::WriteAllText((Join-Path $workspace 'files.m'), $fileProgram, $utf8)
    Check (Invoke-Native $Compiler @('run', 'files.m')) 0 $sampleText
    Check (Invoke-Native $Compiler @('build', 'files.m', '-o', 'files.exe', '-O3')) 0 ''
    Check (Invoke-Native '.\files.exe' @()) 0 $sampleText
    if ([IO.File]::ReadAllText((Join-Path $workspace $textName), $utf8) -cne $sampleText) {
        throw 'Expected the text file to contain the replacement UTF-8 text.'
    }
    [IO.File]::WriteAllBytes((Join-Path $workspace 'invalid-text.txt'), [byte[]] @(0xC0, 0x80))
    [IO.File]::WriteAllText((Join-Path $workspace 'bad-text.m'), 'main() { print(readText("invalid-text.txt")) }', $utf8)
    Check-RuntimeError 'bad-text.m' 'text file must be valid UTF-8.'

    $largeFrame = @('main() {') + (1..1100 | ForEach-Object { "    int item$_ = $_" }) +
        @('    print(item1100)', '}')
    [IO.File]::WriteAllLines((Join-Path $workspace 'stack.m'), [string[]] $largeFrame, $utf8)
    Check (Invoke-Native $Compiler @('run', 'stack.m')) 0 '1100'

    $hangul = [string][char]0xBA38 + [char]0xB7AD
    $emoji = [char]::ConvertFromUtf32(0x1F600)
    $spaceDir = Join-Path $workspace "$hangul source"
    $null = New-Item -ItemType Directory -Path $spaceDir
    $inputFile = Join-Path $spaceDir "$hangul file.m"
    $outputFile = Join-Path $spaceDir "$hangul native.exe"
    $source = @"
// UTF-8 with BOM and multiple statements
void main(
) {
    print(
        "$hangul $emoji" // trailing comment
    )
    print("Second line")
}
"@
    [IO.File]::WriteAllText($inputFile, $source, [System.Text.UTF8Encoding]::new($true))
    $expected = "$hangul $emoji`nSecond line"
    Check (Invoke-Native $Compiler @('run', $inputFile)) 0 $expected
    Check (Invoke-Native $Compiler @('build', $inputFile, '-o', $outputFile)) 0 ''
    Check (Invoke-Native $outputFile @()) 0 $expected
    $binary = [IO.File]::ReadAllBytes($outputFile)
    $pe = [BitConverter]::ToInt32($binary, 0x3c)
    if ([BitConverter]::ToUInt32($binary, $pe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($binary, $pe + 4) -ne 0x8664) {
        throw 'Expected an x64 PE executable.'
    }

    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    Check (Invoke-Native 'cmd.exe' @('/d', '/c', 'moulang run hello.m')) 0 'Hello Moulang!'

    Check-Error 'token.m' 'main() { @ }' 'token\.m:1:10: error ML1001'
    Check-Error 'string.m' 'main() { print("hi) }' 'ML1002'
    Check-Error 'syntax.m' 'main() { print("hi")' 'ML2001'
    Check-Error 'separator.m' 'main() { print("a") print("b") }' 'ML2001'
    Check-Error 'empty.m' '// no entry point' 'ML3001'
    Check-Error 'duplicate.m' "main() {}`nvoid main() {}" 'duplicate\.m:2:6: error ML3002'
    Check-Error 'overflow.m' 'main() { print(9223372036854775808) }' 'ML3003'
    Check-Error 'missing-variable.m' 'main() { print(missing) }' 'ML3004'
    Check-Error 'invalid-operation.m' 'main() { print(true + 1) }' 'ML3005'
    Check-Error 'duplicate-variable.m' "main() { int a = 1`nvar a = 2 }" 'ML3006'
    Check-Error 'invalid-initializer.m' 'main() { int a = false }' 'ML3007'
    Check-Error 'invalid-assignment.m' "main() { var a = 1`na = false }" 'ML3007'
    Check-Error 'unknown-call.m' 'main() { print(missing()) }' 'ML3010'
    Check-Error 'argument-type.m' "int f(int x) { return x }`nmain() { print(f(true)) }" 'ML3011'
    Check-Error 'argument-count.m' "int f(int x) { return x }`nmain() { print(f()) }" 'ML3011'
    Check-Error 'void-value.m' "void f() {}`nmain() { print(f()) }" 'ML3012'
    Check-Error 'missing-return.m' "int f(bool b) { if (b) { return 1 } }`nmain() {}" 'ML3013'
    Check-Error 'return-type.m' "bool f() { return 1 }`nmain() {}" 'ML3013'
    Check-Error 'invalid-condition.m' 'main() { if (1) {} }' 'ML3014'
    Check-Error 'invalid-limit.m' 'main() { for (i=0 in i++(true)) {} }' 'ML3014'
    Check-Error 'outside-loop.m' 'main() { break }' 'ML3015'
    Check-Error 'recursive-inference.m' "def f() { return f() }`nmain() {}" 'ML3016'
    Check-Error 'invalid-main.m' 'int main() { return 1 }' 'ML3017'
    Check-Error 'outside-scope.m' "main() { if (true) { int local = 1 }`nprint(local) }" 'ML3004'
    Check-Error 'slice-element.m' 'main() { int a[] = [true] }' 'ML3018'
    Check-Error 'slice-index.m' 'main() { print([1][true]) }' 'ML3018'
    Check-Error 'slice-append.m' 'main() { var a = append([1], false) }' 'ML3018'
    Check-Error 'null-assignment.m' 'main() { string a = null }' 'ML3007'
    Check-Error 'null-inference.m' 'main() { var a = null }' 'ML3019'
    Check-Error 'null-default.m' "main() { int? a = null`nprint(a ?? true) }" 'ML3019'
    Check-Error 'unsafe-null.m' "main() { int? a = null`nprint(a) }" 'ML3018'
    Check-Error 'stale-null-proof.m' "main() { int? a = 1`nif (a != null) { a = null`nprint(a) } }" 'ML3018'
    Check-Error 'stale-loop-proof.m' "main() { int? a = 1`nif (a != null) { while (true) { print(a)`na = null } } }" 'ML3018'
    Check-Error 'narrow-float.m' 'main() { float value = 16777217 }' 'ML3007'
    Check-Error 'narrow-short.m' 'main() { short value = 32768 }' 'ML3007'
    Check-Error 'narrow-int.m' 'main() { int value = 2.5 }' 'ML3007'
    Check-Error 'narrow-argument.m' "void f(float x) {}`nmain() { f(16777217) }" 'ML3011'
    Check-Error 'narrow-return.m' "float f() { return 16777217 }`nmain() {}" 'ML3013'
    Check-Error 'cast-argument.m' 'main() { print(int("123")) }' 'ML3020'
    Check-Error 'cast-arity.m' 'main() { print(float()) }' 'ML3020'
    Check-Error 'real-exponent.m' 'main() { print(1e+) }' 'ML1003'
    Check-Error 'real-range.m' 'main() { print(1e999) }' 'ML3003'
    Check-Error 'struct-type.m' 'main() { Unknown value = 1 }' 'ML3021'
    Check-Error 'struct-field-type.m' "struct Node { Missing field }`nmain() {}" 'ML3021'
    Check-Error 'struct-field.m' "struct Point { int x }`nmain() { print(Point(1).y) }" 'ML3022'
    Check-Error 'struct-base.m' 'main() { print((10).x) }' 'ML3022'
    Check-Error 'struct-arity.m' "struct Point { int x }`nmain() { Point p = Point() }" 'ML3023'
    Check-Error 'struct-initializer.m' "struct Point { int x }`nmain() { Point p = Point(true) }" 'ML3023'
    Check-Error 'struct-rounded-literal.m' "struct Point { float x }`nmain() { Point p = Point(16777217) }" 'ML3023'
    Check-Error 'struct-implicit-narrow.m' "struct Point { float x }`nmain() { int n = 10`nPoint p = Point(n) }" 'ML3023'
    Check-Error 'struct-duplicate-field.m' "struct Point { int x`nint x }`nmain() {}" 'ML3024'
    Check-Error 'struct-duplicate.m' "struct Point {}`nstruct Point {}`nmain() {}" 'ML3024'
    Check-Error 'struct-cycle.m' "struct A { B b }`nstruct B { A a }`nmain() {}" 'ML3025'
    Check-Error 'struct-self-cycle.m' "struct Node { Node next }`nmain() {}" 'ML3025'
    Check-Error 'struct-nominal.m' "struct A { int x }`nstruct B { int x }`nmain() { A a = B(1) }" 'ML3007'
    Check-Error 'struct-function-collision.m' "struct Point {}`nint Point() { return 0 }`nmain() {}" 'ML3017'
    Check-Error 'field-write-type.m' "struct Point { int x }`nmain() { var p = Point(1)`np.x = true }" 'ML3007'
    Check-Error 'bitwise-real.m' 'main() { print(1.5 & 1) }' 'ML3005'
    Check-Error 'conditional-type.m' 'main() { print(true ? "x" : 1) }' 'ML3026'
    Check-Error 'foreach-scalar.m' 'main() { foreach (value in 3) {} }' 'ML3027'
    Check-Error 'append-view.m' 'main() { var a = append([1][0:1], 2) }' 'ML3018'
    Check-Error 'unsafe-member.m' "struct Point { int x }`nmain() { Point? p = null`nprint(p.x) }" 'ML3022'
    Check-Error 'nullable-struct-cycle.m' "struct Node { Node? next }`nmain() {}" 'ML3025'
    Check-Error 'file-type.m' 'main() { print(readText(1)) }' 'ML3011'
    Check-Error 'file-void.m' 'main() { print(writeText("x", "y")) }' 'ML3012'
    Check-Error 'fixed-length.m' 'main() { int a[2] = [1] }' 'ML3032'
    Check-Error 'fixed-copy.m' "main() { int a[1] = [1]`nint b[2] = a }" 'ML3007'
    Check-Error 'borrow-write.m' "main() { int a[1] = [1]`nvar view = a[0:1]`na[0] = 2 }" 'ML3033'
    Check-Error 'borrow-copy-write.m' "main() { int a[1] = [1]`nvar view = a[0:1]`nvar copy = view`na = [2] }" 'ML3033'
    Check-Error 'borrow-return.m' "def escape() { int a[1] = [1]`nreturn a[0:1] }`nmain() {}" 'ML3034'
    Check-Error 'borrow-reassign.m' "main() { int a[1] = [1]`nvar view = a[0:1]`nview = a[0:1] }" 'ML3034'
    Check-Error 'borrow-temporary.m' "main() { int a[1] = [1]`nvar view = (true ? a : a)[0:1] }" 'ML3034'
    Check-Error 'class-private-field.m' "class A { int x = 1 }`nmain() { print((new A()).x) }" 'ML3035'
    Check-Error 'class-private-method.m' "class A { int x() { return 1 } }`nmain() { print((new A()).x()) }" 'ML3035'
    Check-Error 'class-private-constructor.m' "class A { A() {} }`nmain() { var a = new A() }" 'ML3035'
    Check-Error 'class-call-without-new.m' "class A {}`nmain() { var a = A() }" 'ML3036'
    Check-Error 'class-no-default.m' "class A { int x }`nmain() { var a = new A() }" 'ML3036'
    Check-Error 'class-missing-init.m' "class A { int x`npublic A() {} }`nmain() {}" 'ML3037'
    Check-Error 'class-constructor-read.m' "class A { int x`npublic A() { x = this.x } }`nmain() {}" 'ML3004'
    Check-Error 'class-argument.m' "class A { public A(int x) {} }`nmain() { var a = new A(false) }" 'ML3011'
    Check-Error 'struct-new.m' "struct A {}`nmain() { var a = new A() }" 'ML3036'
    Check-Error 'class-base-cycle.m' "class A : B {}`nclass B : A {}`nmain() {}" 'ML3038'
    Check-Error 'class-base-arity.m' "class A { public A(int x) {} }`nclass B : A {}`nmain() {}" 'ML3038'
    Check-Error 'class-base-override.m' "class A { public int f() { return 1 } }`nclass B : A { public int f() { return 2 } }`nmain() {}" 'ML3038'
    Check-Error 'class-private-inherited.m' "class A { int x = 1 }`nclass B : A { public int f() { return x } }`nmain() {}" 'ML3035'
    Check-Error 'class-multiple-base.m' "class A {}`nclass B {}`nclass C : A, B {}`nmain() {}" 'ML3038'

    [IO.File]::WriteAllBytes((Join-Path $workspace 'invalid-utf8.m'), [byte[]] @(0xC0, 0x80))
    $invalid = Invoke-Native $Compiler @('run', 'invalid-utf8.m')
    if ($invalid.Code -ne 1 -or $invalid.Text -notmatch 'ML1000') {
        throw "Expected invalid UTF-8 diagnostic, got $($invalid.Text)"
    }
    $missing = Invoke-Native $Compiler @('run', 'missing.m')
    if ($missing.Code -ne 1 -or $missing.Text -notmatch 'Cannot open source file') {
        throw "Expected missing-file diagnostic, got $($missing.Text)"
    }
    $usage = Invoke-Native $Compiler @('build', 'hello.m', '-o')
    if ($usage.Code -ne 2 -or $usage.Text -notmatch '^Usage:') {
        throw "Expected CLI usage diagnostic, got $($usage.Text)"
    }
    $optimizationUsage = Invoke-Native $Compiler @('build', 'hello.m', '-O2', '-O3')
    if ($optimizationUsage.Code -ne 2 -or $optimizationUsage.Text -notmatch '^Usage:') {
        throw "Expected duplicate optimization flag diagnostic, got $($optimizationUsage.Text)"
    }
    $overwrite = Invoke-Native $Compiler @('build', 'hello.m', '-o', 'hello.m')
    if ($overwrite.Code -ne 1 -or $overwrite.Text -notmatch 'overwrite the source file') {
        throw "Expected source overwrite diagnostic, got $($overwrite.Text)"
    }
    Check (Invoke-Native $Compiler @('run', 'hello.m')) 0 'Hello Moulang!'
}
finally {
    Pop-Location
    # The workspace is a fresh child created by this invocation.
    $resolved = (Resolve-Path -LiteralPath $workspace).Path
    if ((Split-Path -Parent $resolved) -eq (Resolve-Path -LiteralPath $TestRoot).Path) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
