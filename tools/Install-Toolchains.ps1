param(
    [ValidateSet('llvm', 'mingw')] [string[]] $Component = @('llvm', 'mingw'),
    [string] $Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) '.toolchain')
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$lock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'toolchains.json') | ConvertFrom-Json
$cache = (New-Item -ItemType Directory -Force -Path $Destination).FullName
$tar = Join-Path $env:SystemRoot 'System32/tar.exe'
foreach ($name in $Component) {
    $entry = $lock.$name
    $archive = Join-Path $cache $entry.archive
    if (!(Test-Path -LiteralPath $archive)) {
        $download = Join-Path $cache ($entry.archive + '.' + [guid]::NewGuid().ToString('N') + '.download')
        Invoke-WebRequest -Uri $entry.url -OutFile $download
        if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ine $entry.sha256) {
            throw "SHA-256 mismatch for $name. The download was retained and will not be executed."
        }
        Move-Item -LiteralPath $download -Destination $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $entry.sha256) {
        throw "Cached $name archive has the wrong SHA-256. Use a fresh cache directory."
    }
    $directory = Join-Path $cache $entry.directory
    $stamp = Join-Path $directory '.moulang-verified-sha256'
    $complete = (Test-Path -LiteralPath $stamp) -and ((Get-Content -Raw -LiteralPath $stamp).Trim() -ieq $entry.sha256)
    foreach ($file in $entry.requiredFiles) {
        $complete = $complete -and (Test-Path -LiteralPath (Join-Path $directory $file) -PathType Leaf)
    }
    if (!$complete) {
        $members = @(& $tar -tf $archive)
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $name archive." }
        foreach ($member in $members) {
            if ($member -match '^[/\\]|^[A-Za-z]:|(^|[/\\])\.\.([/\\]|$)' -or
                (!$member.StartsWith($entry.directory + '/') -and $member.TrimEnd('/') -ne $entry.directory)) {
                throw "Unexpected path in the verified $name archive."
            }
        }
        & $tar -xf $archive -C $cache
        if ($LASTEXITCODE -ne 0) { throw "Cannot extract $name archive." }
        foreach ($file in $entry.requiredFiles) {
            if (!(Test-Path -LiteralPath (Join-Path $directory $file) -PathType Leaf)) {
                throw "The extracted $name archive lacks a required file."
            }
        }
        [IO.File]::WriteAllText($stamp, $entry.sha256, [Text.UTF8Encoding]::new($false))
    }
    Write-Output "Verified $name $($entry.version) archive SHA-256 and installation."
}
