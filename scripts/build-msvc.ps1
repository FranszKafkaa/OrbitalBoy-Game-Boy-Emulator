param(
    [switch]$RunTests,
    [string]$Configuration = "RelWithDebInfo",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-VsDevCmd {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            $candidate = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "VsDevCmd.bat nao encontrado. Instale Visual Studio Build Tools/C++ ou ajuste o script."
}

$vsDevCmd = Resolve-VsDevCmd

if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat nao encontrado em '$vsDevCmd'. Ajuste o caminho no script."
}

$buildCommand = "cmake --build $BuildDir --config $Configuration"
$testCommand = if ($RunTests) { " && .\$BuildDir\gbemu_tests.exe" } else { "" }

Push-Location $repoRoot
try {
    cmd /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && $buildCommand$testCommand"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}