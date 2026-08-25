<#
.SYNOPSIS
    Assembles the install layout in output\ from the two build trees.

.DESCRIPTION
    The engine tree (~700 MB, almost all of it Alex) is linked in by directory junction
    rather than copied, unless -Copy is given. Junctions need no elevation and keep a
    rebuild instant; the installer packages the real files.

    output\
      ClassicMacSAPI5.dll        32-bit SAPI 5 engine
      ClassicMacConfig.exe       settings utility
      cmv_speak.exe              speaks through the registered SAPI 5 stack
      cmv_sapitest.exe           renders every registered voice to WAV
      cmv_directtest.exe         drives the DLL without registration
      x64\ClassicMacSAPI5.dll    64-bit SAPI 5 engine
      x64\cmv_speak.exe
      x64\cmv_sapitest.exe
      x64\cmv_directtest.exe
      engine\panthera_host.exe   the 32-bit engine host
      engine\leopard\            MacinTalk, SpeechDictionary and all 24 voices
#>
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Output,
    [switch]$Copy
)

$ErrorActionPreference = 'Stop'
if (-not $Output) { $Output = Join-Path $Root 'output' }

$x86 = Join-Path $Root 'build_x86\bin\Release'
$x64 = Join-Path $Root 'build_x64\bin\Release'
$data = Join-Path $Root 'bin'

foreach ($required in @($x86, $x64, $data)) {
    if (-not (Test-Path $required)) { throw "missing $required - build first" }
}

# The host stays resident inside whichever program last spoke, and while it is alive its
# executable cannot be replaced. Stopping it is safe: the next utterance starts a fresh
# one.
Get-Process -Name 'panthera_host' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

New-Item -ItemType Directory -Force -Path $Output | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Output 'x64') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Output 'engine') | Out-Null

Copy-Item (Join-Path $x86 'ClassicMacSAPI5.dll')  $Output -Force
Copy-Item (Join-Path $x86 'ClassicMacConfig.exe') $Output -Force
Copy-Item (Join-Path $x86 'cmv_speak.exe')        $Output -Force
Copy-Item (Join-Path $x86 'cmv_sapitest.exe')     $Output -Force
Copy-Item (Join-Path $x86 'cmv_directtest.exe')   $Output -Force
Copy-Item (Join-Path $x64 'ClassicMacSAPI5.dll')  (Join-Path $Output 'x64') -Force
Copy-Item (Join-Path $x64 'cmv_speak.exe')        (Join-Path $Output 'x64') -Force
Copy-Item (Join-Path $x64 'cmv_sapitest.exe')     (Join-Path $Output 'x64') -Force
Copy-Item (Join-Path $x64 'cmv_directtest.exe')   (Join-Path $Output 'x64') -Force

Copy-Item (Join-Path $data '_panthera\panthera_host.exe') (Join-Path $Output 'engine') -Force

$target = Join-Path $Output 'engine\leopard'
$source = Join-Path $data 'leopard'
if (Test-Path $target) {
    $item = Get-Item $target -Force
    if ($item.LinkType) {
        # Remove-Item asks whether to delete the junction's contents; this does not.
        [System.IO.Directory]::Delete($target, $false)
    } else {
        Remove-Item $target -Recurse -Force
    }
}
if ($Copy) {
    Copy-Item $source $target -Recurse -Force
} else {
    cmd /c mklink /J "$target" "$source" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'could not create a junction for the engine tree' }
}

Write-Host "staged to $Output"
Get-ChildItem $Output | Select-Object Mode, Name, Length
