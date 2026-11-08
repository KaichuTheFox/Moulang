param(
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    [string] $BuildDirectory = 'build/windows-release'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if (!$visualStudio) { throw 'Install the x64 Visual Studio C++ build tools.' }
    $devcmd = Join-Path $visualStudio 'Common7/Tools/VsDevCmd.bat'
    $null = New-Item -ItemType Directory -Force build
    $environmentScript = 'build/moulang-dev-env-' + [guid]::NewGuid().ToString('N') + '.cmd'
    $batch = '@echo off' + "`r`n" + 'call "' + $devcmd + '" -arch=x64 -host_arch=x64 >nul' + "`r`n" +
        'if errorlevel 1 exit /b 1' + "`r`n" + 'set' + "`r`n"
    [IO.File]::WriteAllText((Join-Path $repo $environmentScript), $batch, [Text.UTF8Encoding]::new($false))
    $environment = & cmd.exe /d /c $environmentScript.Replace('/', '\')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the MSVC environment.' }
    foreach ($line in $environment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    $env:VSLANG = '1033'
    $probe = Join-Path $repo ('build/include-probe-' + [guid]::NewGuid().ToString('N') + '.cpp')
    [IO.File]::WriteAllText($probe, '#include <stddef.h>', [Text.UTF8Encoding]::new($false))
    $includeOutput = & cl.exe /nologo /c /showIncludes (('/Fo' + $probe) + '.obj') $probe 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'MSVC include-prefix detection failed.' }
    $includePrefix = $null
    foreach ($line in $includeOutput) {
        if ($line.ToString() -match '^(.+?)[A-Za-z]:[\\/].*[\\/]stddef[.]h\s*$') {
            $includePrefix = $Matches[1].TrimEnd()
            break
        }
    }
    if (!$includePrefix) { throw 'Cannot detect the localized MSVC include prefix.' }
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $cmake = if ($cmakeCommand) { $cmakeCommand.Source } else {
        Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    }
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    $lock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'toolchains.json') | ConvertFrom-Json
    $llvm = (Resolve-Path (Join-Path '.toolchain' $lock.llvm.directory)).Path
    $mingw = (Resolve-Path (Join-Path '.toolchain' $lock.mingw.directory)).Path
    & $cmake -S . -B $BuildDirectory -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" -DCMAKE_TRY_COMPILE_CONFIGURATION=Release `
        "-DLLVM_DIR=$llvm/lib/cmake/llvm" "-DCMAKE_LINKER=$llvm/bin/lld-link.exe" "-DMOULANG_MINGW_ROOT=$mingw" `
        "-DMOULANG_MSVC_INCLUDE_PREFIX=$includePrefix"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build $BuildDirectory --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'Compiler build failed.' }
    & $ctest --test-dir $BuildDirectory --output-on-failure -C $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
}
finally { Pop-Location }
