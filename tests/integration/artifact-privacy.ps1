param(
    [Parameter(Mandatory = $true)] [string] $Scanner,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)

$ErrorActionPreference = 'Stop'
$root = New-Item -ItemType Directory -Force -Path $TestRoot
$workspace = New-Item -ItemType Directory -Path (Join-Path $root.FullName ([Guid]::NewGuid().ToString('N')))
$windowsPath = @('C:', 'Users', 'privacy-fixture', 'source.cpp') -join '\'
$cases = @(
    @{ Name = 'clean'; Bytes = [Text.Encoding]::UTF8.GetBytes('Moulang 0.1.1'); Expected = 0 },
    @{ Name = 'utf8'; Bytes = [Text.Encoding]::UTF8.GetBytes($windowsPath); Expected = 1 },
    @{ Name = 'utf16'; Bytes = [Text.Encoding]::Unicode.GetBytes($windowsPath); Expected = 1 },
    @{ Name = 'utf16-odd'; Bytes = ([byte[]] @(255) + [Text.Encoding]::Unicode.GetBytes($windowsPath)); Expected = 1 },
    @{ Name = 'forward'; Bytes = [Text.Encoding]::UTF8.GetBytes($windowsPath.Replace('\', '/')); Expected = 1 },
    @{ Name = 'unix'; Bytes = [Text.Encoding]::UTF8.GetBytes('/home/privacy-fixture/source.cpp'); Expected = 1 }
)
foreach ($case in $cases) {
    $file = Join-Path $workspace.FullName ($case.Name + '.bin')
    [IO.File]::WriteAllBytes($file, $case.Bytes)
    $report = & $Scanner -Artifact $file -ReportOnly
    if ($report.UserPathMatches -ne $case.Expected -or $report.Passed -ne ($case.Expected -eq 0)) {
        throw "Unexpected scan result for $($case.Name)."
    }
}
$buildPath = Join-Path $workspace.FullName 'build-prefix.bin'
[IO.File]::WriteAllBytes($buildPath, [Text.Encoding]::Unicode.GetBytes('X:/build-agent/workspace/runtime.cpp'))
$report = & $Scanner -Artifact $buildPath -ForbiddenPrefix 'X:\build-agent\workspace' -ReportOnly
if ($report.ForbiddenPrefixMatches -ne 1 -or $report.Passed) { throw 'Build prefix was not detected.' }
$rejected = $false
try { $null = & $Scanner -Artifact $buildPath -ForbiddenPrefix 'X:/build-agent/workspace' }
catch {
    if ($_.Exception.Message -notmatch '^Artifact privacy check failed') { throw }
    $rejected = $true
}
if (!$rejected) { throw 'The release gate accepted an unsafe fixture.' }
$null = & $Scanner -Artifact (Join-Path $workspace.FullName 'clean.bin')
Write-Output 'PASS: clean artifact, UTF-8, UTF-16LE, unaligned strings, separators, custom build prefix, and rejection gate.'
