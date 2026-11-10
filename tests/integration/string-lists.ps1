param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"

$source = @'
str[] create() {
    str word = "A" + "2"
    return [word, "B", "B", "\u{AC00}"]
}
str[] empty() { return [] }
void change(str[] list) {
    list.insert(0, "Z")
    list[-1] = "\u{D55C}"
}
int scope(str[] values) {
    values.append("one") {
        int result = len(values)
        return result
    }
}
main() {
    var list = create()
    str[] alias = list
    change(alias)
    list.remove("B")
    list.insert(-1, "C")
    list.insert(len(list), "X")
    list.sort() {
        print(list)
    }
    str last = alias.pop()
    print(last, len(list), list[0], list[-1], type(alias))
    alias.append("quote\"\\\n\0")
    print(list)
    str[] zero = []
    zero = empty()
    print(zero, scope(zero), len(zero))
    int previous[] = [1, 2]
    var updated = append(previous, 3)
    print(previous[1], updated[2])
}
'@
$hangul = [char]::ConvertFromUtf32(0xD55C)
$expected = '["A2", "B", "C", "X", "Z", "' + $hangul + '"]' + "`n" +
    "$hangul 5 A2 Z str[]`n" +
    '["A2", "B", "C", "X", "Z", "quote\"\\\n\0"]' + "`n" +
    '["one"] 1 1' + "`n2 3`n"
$r = Invoke-Moulang 'lists.m' $source
if ($r.Code -ne 0 -or $r.Error -or $r.Out -cne $expected) {
    throw "Unexpected list output: $($r.Code), [$($r.Out)], [$($r.Error)]."
}

$compileErrors = @(
    @{ Code = 'ML3018'; Text = 'main() { var x = ["A", 1] }' },
    @{ Code = 'ML3018'; Text = 'main() { str[] x = [1] }' },
    @{ Code = 'ML3018'; Text = "main() {`n    int x[1] = [""A""]`n}" },
    @{ Code = 'ML3011'; Text = "main() {`n    str[] x = []`n    x.append(1)`n}" },
    @{ Code = 'ML3012'; Text = "main() {`n    str[] x = []`n    var y = x.sort()`n}" },
    @{ Code = 'ML3034'; Text = "main() {`n    str x = ""A""`n    x[0] = ""B""`n}" },
    @{ Code = 'ML3043'; Text = "class A { public void sort() {} }`nmain() {`n    A a = new A()`n    a.sort() {}`n}" }
)
foreach ($case in $compileErrors) {
    $r = Invoke-Moulang 'bad-list.m' $case.Text 'check'
    if ($r.Code -ne 1 -or $r.Error -notmatch $case.Code) { throw "Expected $($case.Code): $($r.Error)" }
}

foreach ($case in @(
    @{ Text = 'values.pop() { print("should not run") }'; Pattern = 'pop an empty string list' },
    @{ Text = 'values.remove("missing") { print("should not run") }'; Pattern = 'does not contain' },
    @{ Text = 'values.insert(1, "bad") { print("should not run") }'; Pattern = 'index out of bounds' },
    @{ Text = 'print(values[-1])'; Pattern = 'index out of bounds' },
    @{ Text = 'values[0] = "bad"'; Pattern = 'index out of bounds' }
)) {
    $r = Invoke-Moulang 'bad-list.m' "main() {`n    str[] values = []`n    $($case.Text)`n}"
    if ($r.Code -ne 1 -or $r.Out -or $r.Error -notmatch (':3:\d+: Moulang runtime: .*' + $case.Pattern)) {
        throw "Expected checked list failure: $($r.Code), [$($r.Out)], [$($r.Error)]."
    }
}
