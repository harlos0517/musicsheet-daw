# Configure and build cpp/ from any PowerShell (VS Code terminal included).
#
#   .\build.ps1                  # debug build
#   .\build.ps1 -Run             # debug build, then start the app
#   .\build.ps1 -Test            # debug build, then run the unit tests
#   .\build.ps1 release
#   .\build.ps1 -Fresh           # throw away the CMake cache first
#
# Enters the Visual Studio x64 developer environment when cl/ninja are not
# already on PATH. That only changes this PowerShell session's environment.

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'debug',
    [switch]$Fresh,
    [switch]$Run,
    [switch]$Test
)

# Not 'Stop': in Windows PowerShell 5.1 that turns cmake's stderr warnings into
# terminating errors when output is redirected. Failures are checked via $LASTEXITCODE.

function Enter-VsDevEnvironment {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2022 Build Tools with the C++ workload."
    }
    # Newest stable (non-preview) install that has the x64 C++ tools.
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) {
        throw "No Visual Studio install with the C++ x64 tools found."
    }
    $devShell = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
    Write-Host "Entering VS dev shell: $vsPath"
    # Launch-VsDevShell calls vswhere by name; put it on PATH to avoid a harmless error line.
    $env:PATH = "$(Split-Path $vswhere);$env:PATH"
    & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}

if (-not ((Get-Command cl -ErrorAction SilentlyContinue) -and (Get-Command ninja -ErrorAction SilentlyContinue))) {
    Enter-VsDevEnvironment
}

Push-Location $PSScriptRoot
try {
    $configureArgs = @('--preset', $Preset)
    if ($Fresh) { $configureArgs += '--fresh' }

    cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }

    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

    $config = if ($Preset -eq 'debug') { 'Debug' } else { 'Release' }
    $exe = Join-Path $PSScriptRoot "build\$Preset\src\app\MusicsheetDawApp_artefacts\$config\Musicsheet DAW.exe"
    Write-Host "Built: $exe"

    if ($Test) {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)." }
    }

    if ($Run) { Start-Process $exe }
}
finally {
    Pop-Location
}
