param(
    [Parameter(Mandatory = $true)] [string] $Verifier,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
$root = New-Item -ItemType Directory -Force -Path $TestRoot
foreach ($case in 'traversal', 'tampered', 'unlisted', 'duplicate', 'missing-notice') {
    $directory = (New-Item -ItemType Directory -Path (Join-Path $root.FullName ([guid]::NewGuid().ToString('N')))).FullName
    $file = Join-Path $directory 'sample.txt'
    [IO.File]::WriteAllText($file, 'fixture')
    $entry = [pscustomobject]@{ path = 'sample.txt'; sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash }
    if ($case -eq 'traversal') { $entry.path = '../outside.txt' }
    if ($case -eq 'tampered') { $entry.sha256 = '0' * 64 }
    if ($case -eq 'unlisted') { [IO.File]::WriteAllText((Join-Path $directory 'unexpected.txt'), 'extra') }
    $entries = @($entry)
    if ($case -eq 'duplicate') { $entries += $entry }
    [pscustomobject]@{ files = $entries } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $directory 'package-manifest.json') -Encoding utf8
    $rejected = $false
    try { & $Verifier -Root $directory }
    catch {
        $expected = switch ($case) {
            'traversal' { 'outside the installation' }
            'tampered' { 'integrity mismatch' }
            'unlisted' { 'unlisted files' }
            'duplicate' { 'more than once' }
            'missing-notice' { 'Missing required third-party notice' }
        }
        if ($_.Exception.Message -notmatch $expected) { throw }
        $rejected = $true
    }
    if (!$rejected) { throw "The install verifier accepted $case input." }
}
Write-Output 'PASS: invalid manifests and missing notices are rejected before compiler execution.'
