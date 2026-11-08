param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $TestRoot
)
$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$root = (New-Item -ItemType Directory -Force -Path $TestRoot).FullName
$workspace = (New-Item -ItemType Directory -Path (Join-Path $root ([guid]::NewGuid().ToString('N')))).FullName
$env:TEMP = $workspace
$env:TMP = $workspace

function Invoke-Bytes([string] $Program, [string[]] $Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.Arguments = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $start.WorkingDirectory = $workspace
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardErrorEncoding = $utf8
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $null = $process.Start()
    $errorOutput = $process.StandardError.ReadToEndAsync()
    $bytes = [IO.MemoryStream]::new()
    $process.StandardOutput.BaseStream.CopyTo($bytes)
    $process.WaitForExit()
    $result = @{ Code = $process.ExitCode; Bytes = $bytes.ToArray(); Error = $errorOutput.GetAwaiter().GetResult() }
    $process.Dispose()
    $bytes.Dispose()
    return $result
}

function Assert-Bytes([byte[]] $Actual, [byte[]] $Expected, [string] $Label) {
    if ([Convert]::ToBase64String($Actual) -cne [Convert]::ToBase64String($Expected)) {
        throw "Byte mismatch: $Label."
    }
}

function Assert-Success($Result) {
    if ($Result.Code -ne 0 -or $Result.Error) { throw "Expected success, got $($Result.Code): $($Result.Error)" }
}

$modules = (New-Item -ItemType Directory -Path (Join-Path $workspace 'modules')).FullName
$accent = [char] 0xE9
[IO.File]::WriteAllText((Join-Path $modules ($accent + '.m')), 'string imported() { return "A\0B" }', $utf8)
$source = @'
import "modules/\u{E9}.m"
main() {
    print("line\nnext\r\t\\\"")
    print("\x41F")
    print("\xC3\xA9" == "\u{E9}")
    print("\u{1F600}")
    print("A\0B" == "A\x00B")
    print("A\0B" == "A")
    print("")
    print("\u{000000}")
    print("\u{7F}\u{80}\u{7FF}\u{800}\u{D7FF}\u{E000}\u{FFFF}\u{10000}\u{10FFFF}")
    print(imported() == "A\u{0}B")
    writeText("escaped.txt", "A\0B\xC3\xA9\n\u{1F600}")
    print(readText("escaped.txt") == "A\u{0}B\u{E9}\n\u{1f600}")
}
'@
$file = Join-Path $workspace 'escapes.m'
[IO.File]::WriteAllText($file, $source, $utf8)
$emoji = [char]::ConvertFromUtf32(0x1F600)
$boundaries = (@(0x7F,0x80,0x7FF,0x800,0xD7FF,0xE000,0xFFFF,0x10000,0x10FFFF) |
    ForEach-Object { [char]::ConvertFromUtf32($_) }) -join ''
$expected = $utf8.GetBytes('line' + "`n" + 'next' + "`r`t" + '\"' + "`nAF`ntrue`n" + $emoji +
    "`ntrue`nfalse`n`n" + [char]0 + "`n" + $boundaries + "`ntrue`ntrue`n")
$contents = $utf8.GetBytes('A' + [char]0 + 'B' + $accent + "`n" + $emoji)
Assert-Success (Invoke-Bytes $Compiler @('check', $file))
$result = Invoke-Bytes $Compiler @('run', $file)
Assert-Success $result
Assert-Bytes $result.Bytes $expected 'run output with NUL and Unicode'
foreach ($level in '-O0','-O2','-O3') {
    $output = Join-Path $workspace ($level + '.exe')
    Assert-Success (Invoke-Bytes $Compiler @('build', $file, '-o', $output, $level))
    $result = Invoke-Bytes $output @()
    Assert-Success $result
    Assert-Bytes $result.Bytes $expected "$level output"
    Assert-Bytes ([IO.File]::ReadAllBytes((Join-Path $workspace 'escaped.txt'))) $contents "$level file contents"
}

$bad = @(
    @{ Text = '\q'; Code = 'ML1004'; Reason = 'Unknown string escape' },
    @{ Text = '\x'; Code = 'ML1004'; Reason = 'exactly two' },
    @{ Text = '\x0'; Code = 'ML1004'; Reason = 'exactly two' },
    @{ Text = '\xGG'; Code = 'ML1004'; Reason = 'exactly two' },
    @{ Text = '\u'; Code = 'ML1004'; Reason = 'one to six' },
    @{ Text = '\u1234'; Code = 'ML1004'; Reason = 'one to six' },
    @{ Text = '\u{}'; Code = 'ML1004'; Reason = 'one to six' },
    @{ Text = '\u{1'; Code = 'ML1004'; Reason = 'one to six' },
    @{ Text = '\u{12z}'; Code = 'ML1004'; Reason = 'one to six' },
    @{ Text = '\u{1234567}'; Code = 'ML1004'; Reason = 'one to six' },
    @{ Text = '\u{D800}'; Code = 'ML1004'; Reason = 'excluding surrogates' },
    @{ Text = '\u{DFFF}'; Code = 'ML1004'; Reason = 'excluding surrogates' },
    @{ Text = '\u{110000}'; Code = 'ML1004'; Reason = 'at most U\+10FFFF' }
)
foreach ($bytes in '\xFF','\x80','\xC0\xAF','\xE0\x80\x80','\xED\xA0\x80',
    '\xF4\x90\x80\x80','\xF5\x80\x80\x80','\xC3','\xC3A','\xE2\0','\xC3\u{E9}') {
    $bad += @{ Text = $bytes; Code = 'ML1005'; Reason = 'valid UTF-8' }
}
$index = 0
foreach ($case in $bad) {
    $name = 'invalid ' + $accent + ' ' + (++$index) + '.m'
    $invalid = Join-Path $workspace $name
    # A non-ASCII source character before the escape must count as one column.
    [IO.File]::WriteAllText($invalid, "main() {`r`n    print(`"" + $accent + $case.Text + "`")`r`n}", $utf8)
    $result = Invoke-Bytes $Compiler @('check', $invalid)
    $pattern = [regex]::Escape($invalid) + ':2:13: error ' + $case.Code + ': .*' + $case.Reason
    if ($result.Code -ne 1 -or $result.Error -notmatch $pattern) {
        throw "Expected precise escape diagnostic for case $index, got $($result.Code): $($result.Error)"
    }
}

$truncated = Join-Path $workspace 'truncated.m'
$truncatedSource = 'main() { print("abc\'
[IO.File]::WriteAllText($truncated, $truncatedSource, $utf8)
$result = Invoke-Bytes $Compiler @('build', $truncated)
$column = $truncatedSource.IndexOf('\') + 1
if ($result.Code -ne 1 -or $result.Error -notmatch (':1:' + $column + ': error ML1004: Incomplete') -or
    (Test-Path -LiteralPath (Join-Path $workspace 'truncated.exe'))) { throw 'Truncated escape was not rejected.' }

$raw = Join-Path $workspace 'raw-nul.m'
[IO.File]::WriteAllText($raw, 'main() { print("' + [char]0 + '") }', $utf8)
$result = Invoke-Bytes $Compiler @('check', $raw)
if ($result.Code -ne 1 -or $result.Error -notmatch ':1:17: error ML1001: Unexpected raw NUL') {
    throw 'Expected a raw-NUL source diagnostic.'
}

$import = Join-Path $workspace 'nul-import.m'
[IO.File]::WriteAllText($import, "import `"modules/ok.m\0.m`"`nmain() {}", $utf8)
$result = Invoke-Bytes $Compiler @('check', $import)
if ($result.Code -ne 1 -or $result.Error -notmatch ':1:8: error ML3028: Import path must not contain NUL') {
    throw "Expected an import NUL diagnostic, got $($result.Error)"
}
foreach ($call in 'print(readText("escaped.txt\0ignored"))','writeText("escaped.txt\0ignored", "lost")') {
    $invalid = Join-Path $workspace ('path-' + [guid]::NewGuid().ToString('N') + '.m')
    [IO.File]::WriteAllText($invalid, 'main() { ' + $call + ' }', $utf8)
    $result = Invoke-Bytes $Compiler @('run', $invalid)
    if ($result.Code -ne 1 -or $result.Error -notmatch 'Moulang runtime: invalid text file path') {
        throw 'A NUL-containing file path must not be silently truncated.'
    }
    Assert-Bytes ([IO.File]::ReadAllBytes((Join-Path $workspace 'escaped.txt'))) $contents 'NUL path preserved existing file'
}
Write-Output 'PASS: escapes, Unicode boundaries, raw output/file bytes, NUL safety, imports, optimization levels, and precise invalid-escape diagnostics.'
