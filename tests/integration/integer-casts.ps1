param(
    [Parameter(Mandatory)] [string] $Compiler,
    [Parameter(Mandatory)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/run-source.ps1"

$cases = @(
    @{ Source = 'short'; Target = 'char'; Operand = 'short(traced(value))'; Bounds = @('0', '255', '-1', '256') },
    @{ Source = 'int'; Target = 'char'; Operand = 'int(traced(value))'; Bounds = @('0', '255', '-1', '256') },
    @{ Source = 'long'; Target = 'char'; Operand = 'traced(value)'; Bounds = @('0', '255', '-1', '256') },
    @{ Source = 'int'; Target = 'short'; Operand = 'int(traced(value))'; Bounds = @('-32768', '32767', '-32769', '32768') },
    @{ Source = 'long'; Target = 'short'; Operand = 'traced(value)'; Bounds = @('-32768', '32767', '-32769', '32768') },
    @{ Source = 'long'; Target = 'int'; Operand = 'traced(value)'; Bounds = @('-2147483648', '2147483647', '-2147483649', '2147483648') }
)

$source = @'
long traced(long value) {
    print("operand")
    return value
}
main() {
    int mode = 0
    long value = 0
    print("input") {
        input(mode)
        input(value)
    }
    if (mode == 0) {
        print(short(-32768) / short(-1), 2147483647 + 1)
        print(int(4294967297.0), char(256.0), short(65535.0), int(2.5), int(3.5))
        print("checked {}") { format(short(7)) }
    }
'@
$lines = @($source -split '\r?\n')
for ($i = 0; $i -lt $cases.Count; $i++) {
    $case = $cases[$i]
    $case.Mode = $i + 1
    $cast = "$($case.Target)($($case.Operand))"
    $line = "    if (mode == $($case.Mode)) { print($cast) }"
    $case.Location = "$($lines.Count + 1):$($line.IndexOf($cast, [StringComparison]::Ordinal) + 1)"
    $lines += $line
}
$constantMode = $cases.Count + 1
$constantLine = "    if (mode == $constantMode) { print(char(260)) }"
$constantLocation = "$($lines.Count + 1):$($constantLine.IndexOf('char', [StringComparison]::Ordinal) + 1)"
$lines += $constantLine, '}'
$source = $lines -join [char]10
$newline = [char]10

function Invoke-CastCase([string] $Executable, [int] $Mode, [string] $Value) {
    Invoke-TestProcess $Executable -InputBytes $utf8.GetBytes("$Mode$newline$Value$newline")
}

foreach ($level in '-O0', '-O2', '-O3') {
    $exe = Join-Path $work "integer-casts$level.exe"
    $build = Invoke-Moulang 'integer-casts.m' $source 'build' -Options @('-o', ('"' + $exe + '"'), $level)
    if ($build.Code -ne 0 -or $build.Error) { throw "Build failed ($level): [$($build.Error)]" }
    $preserved = Invoke-CastCase $exe 0 '0'
    $expected = @('input', '-32768 -2147483648', '1 0 -1 2 4', 'checked 7', '') -join $newline
    if ($preserved.Code -ne 0 -or $preserved.Out -cne $expected -or $preserved.Error) {
        throw "Unexpected arithmetic, real conversion or format result ($level): [$($preserved.Out)], [$($preserved.Error)]."
    }
    foreach ($case in $cases) {
        foreach ($value in $case.Bounds[0..1]) {
            $result = Invoke-CastCase $exe $case.Mode $value
            if ($result.Code -ne 0 -or $result.Out -cne "input`noperand`n$value`n" -or $result.Error) {
                throw "Unexpected result for $($case.Source) to $($case.Target) ($value, $level): [$($result.Out)], [$($result.Error)]."
            }
        }
        $invalid = $case.Bounds[2..3]
        if ($case.Source -eq 'long') { $invalid += '-9223372036854775808', '9223372036854775807' }
        $diagnostic = [regex]::Escape(
            "integer-casts.m:$($case.Location): Moulang runtime: $($case.Source) value is outside the $($case.Target) range."
        )
        foreach ($value in $invalid) {
            $result = Invoke-CastCase $exe $case.Mode $value
            if ($result.Code -ne 1 -or $result.Out -cne "input`noperand`n" -or $result.Error -notmatch $diagnostic) {
                throw "Expected narrowing error for $($case.Source) to $($case.Target) ($value, $level): [$($result.Out)], [$($result.Error)]."
            }
        }
    }
    $constant = Invoke-CastCase $exe $constantMode '0'
    $diagnostic = [regex]::Escape("integer-casts.m:${constantLocation}: Moulang runtime: int value is outside the char range.")
    if ($constant.Code -ne 1 -or $constant.Out -cne "input`n" -or $constant.Error -notmatch $diagnostic) {
        throw "Expected a runtime error for the out-of-range constant ($level): [$($constant.Error)]."
    }
}
Write-Output 'PASS: integer narrowing bounds, cast locations, single evaluation and numeric and output compatibility at all optimization levels.'
