param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot,
    [Parameter(Mandatory)] [string] $Configuration
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$env:MOULANG_ARC_REPORT = '1'

$source = @'
struct Basket { int[] values }

int[] make() {
    return [7, 3, 3, -2]
}

void appendThroughArgument(int[] values) { values.append(12) }
int count(int[] values) { return len(values) }
int count(int value) { return value }

def escapedView() {
    int[] local = [3, 6, 9, 12]
    var view = local[1:4][1:3]
    local[-1] = 0
    return view
}

int first(int[] values) {
    foreach (value in values) {
        if (value == -2) { continue }
        return value
    }
    return 0
}

main() {
    int[] values = make()
    var alias = values
    var original = values[0:4]
    alias[-1] = short(8)
    var beforeSort = values[0:4]
    alias.sort()
    appendThroughArgument(values)
    print(values, original[3], beforeSort[0], beforeSort[3])
    alias.remove(3)
    alias.insert(1, -2)
    print(alias, alias[-1], len(values))
    var beforePop = values[3:5]
    print(alias.pop())
    alias.append(19) { print("after", len(alias)) }
    print(beforePop[1], values[-1])

    int[] ordered = [-2, 4, 5]
    foreach (entry in ordered) {
        if (entry == -2) {
            ordered.sort()
            ordered[1] = 6
        }
        print(entry)
        if (entry == 6) { break }
    }
    print(first(ordered))
    ordered.sort()
    print(ordered)
    int sum = 0
    foreach (point in escapedView()) { sum += point }
    print(sum, escapedView()[0])

    int[] empty = []
    foreach (nothing in empty[0:0]) { print("unexpected") }
    empty.append(2147483647)
    var basket = Basket([1, 2])
    basket.values.append(-2147483648)
    print(empty, basket.values, count([]), count([1, 2]), count(3))

    int legacy[] = [1]
    var left = append(legacy, 2)
    var right = append(legacy, 3)
    print(left[1], right[1])
}
'@
$expected = @(
    '[3, 3, 7, 8, 12] -2 7 8', '[3, -2, 7, 8, 12] 12 5',
    '12', 'after 5', '12 19', '-2', '6', '6', '[-2, 5, 6]', '21 9',
    '[2147483647] [1, 2, -2147483648] 0 2 3', '2 3', ''
) -join [char]10
$report = if ($Configuration -eq 'Debug') {
    '^Moulang ARC debug: live_objects=0 live_bytes=0 class_roots=0 rooted_objects=0 rooted_bytes=0\r?\n$'
} else { '^$' }
$result = Invoke-Moulang 'int-list.m' $source
if ($result.Code -ne 0 -or $result.Out -cne $expected -or $result.Error -notmatch $report) {
    throw "Unexpected int list result ($($result.Code)), [$($result.Out)], [$($result.Error)]."
}

foreach ($change in 'alias.append(3)', 'alias.insert(2, 3)', 'alias.remove(1)', 'alias.pop()', 'alias.sort()') {
    $source = @"
main() {
    int[] values = [2, 1]
    var alias = values
    foreach (item in values) {
        $change
        break
    }
}
"@
    $result = Invoke-Moulang 'mutate.m' $source
    if ($result.Code -ne 1 -or $result.Out -or
        $result.Error -notmatch 'mutate[.]m:5:\d+: Moulang runtime: cannot structurally change a list during foreach') {
        throw "Expected a mutation error for $change ($($result.Code)), [$($result.Out)], [$($result.Error)]."
    }
}

foreach ($case in @(
    @{ Code = 'ML3018'; Text = 'int[] wrong = [true]' },
    @{ Code = 'ML3007'; Text = "var legacy = [1]`nint[] wrong = legacy" },
    @{ Code = 'ML3018'; Text = 'var wrong = append(values, 1)' },
    @{ Code = 'ML3034'; Text = "var view = values[0:1]`nview[0] = 5" }
)) {
    $result = Invoke-Moulang 'int-list-invalid.m' "main() {`nint[] values = [1]`n$($case.Text)`n}" 'check'
    if ($result.Code -ne 1 -or $result.Error -notmatch $case.Code) {
        throw "Expected $($case.Code): [$($result.Error)]."
    }
}

foreach ($case in @(
    @{ Message = 'int list index out of bounds'; Text = 'print(values[-2])' },
    @{ Message = 'int list index out of bounds'; Text = 'values.insert(2, 8)' },
    @{ Message = 'slice range out of bounds'; Text = 'var view = values[0:2]' },
    @{ Message = 'does not contain the value'; Text = 'values.remove(8)' },
    @{ Message = 'cannot pop an empty int list'; Text = "values.pop()`nvalues.pop()" }
)) {
    $result = Invoke-Moulang 'int-list-bounds.m' "main() {`nint[] values = [1]`n$($case.Text)`n}"
    if ($result.Code -ne 1 -or $result.Out -or $result.Error -notmatch $case.Message) {
        throw "Expected $($case.Message): [$($result.Error)]."
    }
}
Write-Output 'PASS: int list mutations, aliasing, snapshot views, ARC and legacy slice behavior.'
