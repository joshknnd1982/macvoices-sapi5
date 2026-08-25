<#
.SYNOPSIS
    Builds both architectures, stages the install layout, and compiles the installer.

.DESCRIPTION
    Everything the project produces, in one command:

        powershell -ExecutionPolicy Bypass -File tools\build_all.ps1

    CMake drives MSVC through the Visual Studio generator, so no vcvarsall call is needed.
    The installer is compiled with Inno Setup 6; pass -Iscc to point at a different copy.

.PARAMETER SkipInstaller
    Build and stage only. Useful when iterating on the engine.

.PARAMETER Clean
    Delete the build trees first.
#>
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Version = '1.0.0',
    [string]$Iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    [switch]$SkipInstaller,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location $Root

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }

if ($Clean) {
    Step 'Cleaning build trees'
    foreach ($d in @('build_x86', 'build_x64', 'output', 'dist')) {
        $p = Join-Path $Root $d
        if (Test-Path $p) { Remove-Item $p -Recurse -Force }
    }
}

Step 'Configuring x86'
cmake -G 'Visual Studio 17 2022' -A Win32 -S . -B build_x86 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'x86 configure failed' }

Step 'Configuring x64'
cmake -G 'Visual Studio 17 2022' -A x64 -S . -B build_x64 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'x64 configure failed' }

Step 'Building x86'
cmake --build build_x86 --config Release
if ($LASTEXITCODE -ne 0) { throw 'x86 build failed' }

Step 'Building x64'
cmake --build build_x64 --config Release
if ($LASTEXITCODE -ne 0) { throw 'x64 build failed' }

Step 'Staging'
& (Join-Path $PSScriptRoot 'stage.ps1') -Root $Root | Out-Null

if ($SkipInstaller) {
    Write-Host "Done. Staged layout is in $(Join-Path $Root 'output')."
    return
}

if (-not (Test-Path $Iscc)) {
    $fallback = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path $fallback) {
        $Iscc = $fallback
    } else {
        throw "Inno Setup compiler not found. Looked in `"$Iscc`" and `"$fallback`"."
    }
}

Step "Compiling the installer with $Iscc"
$stage = Join-Path $Root 'output'
$script = Join-Path $Root 'installer\classicmac_sapi5.iss'
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'dist') | Out-Null

& $Iscc "/DStageDir=$stage" "/DVersion=$Version" $script
if ($LASTEXITCODE -ne 0) { throw 'installer compilation failed' }

$setup = Join-Path $Root "dist\ClassicMacVoices_Setup_$Version.exe"
if (Test-Path $setup) {
    $size = [math]::Round((Get-Item $setup).Length / 1MB, 1)
    Write-Host ""
    Write-Host "Installer: $setup ($size MB)" -ForegroundColor Green
} else {
    throw "the installer was not produced at $setup"
}
