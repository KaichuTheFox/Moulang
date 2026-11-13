param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot,
    [Parameter(Mandatory)] [string] $Configuration
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"
$env:MOULANG_ARC_REPORT = '1'

$source = @'
str[] source(str[] items) {
    print("source")
    return items
}

str find(str[] values) {
    foreach (value in values) {
        if (value == "a!") { continue }
        return value
    }
    return "missing"
}

main() {
    str[] items = ["a" + "!", "b", "c"]
    var alias = items
    foreach (value in source(items)) {
        if (value == "a!") {
            alias[0] = "replacement"
            alias[1] = "new" + "!"
            items = ["rebound"]
        }
        print(value)
        value = "local"
    }
    print(items)
    alias.append("done")
    print(alias)

    str[][] rows = [["old" + "!"], ["second"]]
    foreach (row in rows) {
        if (row[0] == "old!") {
            rows[0] = ["discarded"]
            rows[1] = ["next"]
            row.append("child")
        }
        foreach (part in row) {
            foreach (inner in row) {
                print(part, inner)
                break
            }
        }
        row.append("after child loops")
    }
    print(rows)

    str[] ordered = ["a!", "b"]
    foreach (item in ordered) {
        ordered.sort()
        if (item == "a!") { continue }
        break
    }
    print(find(ordered))
    ordered.insert(0, "ok")
    print(ordered.pop())
    str[] empty = []
    foreach (item in empty) { print("unexpected") }
    empty.append("after empty")
    print(empty)
}
'@
$expected = @(
    'source', 'a!', 'new!', 'c', '["rebound"]', '["replacement", "new!", "c", "done"]',
    'old! old!', 'child old!', 'next next', '[["discarded"], ["next", "after child loops"]]',
    'b', 'b', '["after empty"]', ''
) -join [char]10
$report = if ($Configuration -eq 'Debug') {
    '^Moulang ARC debug: live_objects=0 live_bytes=0 class_roots=0 rooted_objects=0 rooted_bytes=0\r?\n$'
} else { '^$' }
$result = Invoke-Moulang 'list-foreach.m' $source
if ($result.Code -ne 0 -or $result.Out -cne $expected -or $result.Error -notmatch $report) {
    throw "Unexpected list iteration ($($result.Code)), [$($result.Out)], [$($result.Error)]."
}

foreach ($change in 'alias.append("c")', 'alias.insert(1, "c")', 'alias.remove("a")',
    'alias.pop()', 'alias.sort()', 'mutate(alias)') {
    $source = @"
void mutate(str[] list) { list.append("c") }
main() {
    str[] items = ["b", "a"]
    var alias = items
    foreach (item in items) {
        $change
        break
    }
    print("unexpected")
}
"@
    $line = if ($change -eq 'mutate(alias)') { 1 } else { 6 }
    $result = Invoke-Moulang 'list-mutation.m' $source
    if ($result.Code -ne 1 -or $result.Out -or
        $result.Error -notmatch "list-mutation[.]m:${line}:\d+: Moulang runtime: cannot structurally change a list during foreach") {
        throw "Expected a mutation error for $change ($($result.Code)), [$($result.Out)], [$($result.Error)]."
    }
}

$result = Invoke-Moulang 'nested-mutation.m' @'
main() {
    str[][] rows = [["a"]]
    var alias = rows
    foreach (row in rows) {
        alias.append(["b"])
        break
    }
}
'@
if ($result.Code -ne 1 -or $result.Out -or
    $result.Error -notmatch 'nested-mutation[.]m:5:\d+: Moulang runtime: cannot structurally change a list during foreach') {
    throw "Expected a nested-list mutation error ($($result.Code)), [$($result.Out)], [$($result.Error)]."
}

$result = Invoke-Moulang 'row-mutation.m' @'
main() {
    str[] child = ["a"]
    str[][] rows = [child]
    str[][] other = [child]
    foreach (row in rows) {
        foreach (item in row) {
            other[0].append("b")
            return
        }
    }
}
'@
if ($result.Code -ne 1 -or $result.Out -or
    $result.Error -notmatch 'row-mutation[.]m:7:\d+: Moulang runtime: cannot structurally change a list during foreach') {
    throw "Expected a shared-row mutation error ($($result.Code)), [$($result.Out)], [$($result.Error)]."
}
Write-Output 'PASS: flat and nested list iteration, alias checks, element replacement, loop cleanup and ARC.'
