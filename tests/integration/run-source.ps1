$utf8 = [Text.UTF8Encoding]::new($false, $true)
$root = (New-Item -ItemType Directory -Force -Path $TestRoot).FullName
$work = (New-Item -ItemType Directory -Path (Join-Path $root ([guid]::NewGuid().ToString('N')))).FullName
$env:TEMP = $work
$env:TMP = $work

function Invoke-TestProcess([string] $Program, [string] $Arguments = '', [byte[]] $InputBytes = @()) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.Arguments = $Arguments
    $start.WorkingDirectory = $work
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $process = [Diagnostics.Process]::Start($start)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        $process.StandardInput.BaseStream.Write($InputBytes, 0, $InputBytes.Length)
        $process.StandardInput.Close()
        $process.WaitForExit()
        return @{ Code = $process.ExitCode; Out = $output.GetAwaiter().GetResult(); Error = $errors.GetAwaiter().GetResult() }
    } finally {
        $process.Dispose()
    }
}

function Invoke-Moulang([string] $Name, [string] $Source, [string] $Command = 'run', [byte[]] $InputBytes = @(),
    [string[]] $Options = @()) {
    $file = Join-Path $work $Name
    [IO.File]::WriteAllText($file, $Source, $utf8)
    $arguments = (@($Command, ('"' + $file + '"')) + $Options) -join ' '
    Invoke-TestProcess $Compiler $arguments $InputBytes
}
