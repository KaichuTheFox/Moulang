param(
    [Parameter(Mandatory = $true)]
    [string[]] $Artifact,
    [string[]] $ForbiddenPrefix = @(),
    [switch] $ReportOnly
)

$ErrorActionPreference = 'Stop'
# Report counts, never the matching username, email, or path.
$pattern = '(?i)(?:[a-z]:[\\/]+Users[\\/]+[^\\/\s\x00]+|/(?:Users|home)/[^/\s\x00]+)'
$reports = foreach ($file in $Artifact) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $file).ProviderPath)
    $texts = @(
        @{ Encoding = 'UTF-8'; Text = [Text.Encoding]::UTF8.GetString($bytes) },
        @{ Encoding = 'UTF-16LE-even'; Text = [Text.Encoding]::Unicode.GetString($bytes) }
    )
    if ($bytes.Length -gt 1) {
        $texts += @{ Encoding = 'UTF-16LE-odd'; Text = [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) }
    }
    $homeMatches = 0
    $prefixMatches = 0
    foreach ($text in $texts) {
        $homeMatches += [regex]::Matches($text.Text, $pattern).Count
        foreach ($prefix in $ForbiddenPrefix) {
            if ([string]::IsNullOrWhiteSpace($prefix)) { throw 'A forbidden prefix must not be empty.' }
            $variants = @($prefix.Replace('\', '/'), $prefix.Replace('/', '\')) | Select-Object -Unique
            foreach ($variant in $variants) {
                $prefixMatches += [regex]::Matches($text.Text, [regex]::Escape($variant), 'IgnoreCase').Count
            }
        }
    }
    [pscustomobject]@{
        Artifact = Split-Path -Leaf $file
        Bytes = $bytes.Length
        UserPathMatches = $homeMatches
        ForbiddenPrefixMatches = $prefixMatches
        Passed = ($homeMatches -eq 0 -and $prefixMatches -eq 0)
    }
}
$reports
if (!$ReportOnly -and @($reports | Where-Object { !$_.Passed }).Count) {
    throw 'Artifact privacy check failed. Review the counts above before distribution.'
}
