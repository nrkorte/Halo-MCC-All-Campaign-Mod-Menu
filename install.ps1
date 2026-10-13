# Installs (or removes) HaloModMenu in MCC's win64 folder as version.dll.
#
#   .\install.ps1              copy build\Release\version.dll into the game
#   .\install.ps1 -Uninstall   remove it (do this before playing with anti-cheat on)
#
# Launch MCC with "Play Halo: MCC (Anti-Cheat Disabled)" for the mod to load.

param(
    [switch]$Uninstall,
    [string]$GameDir = 'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\mcc\binaries\win64'
)

$ErrorActionPreference = 'Stop'

$Target   = Join-Path $GameDir 'version.dll'
$BuildDll = Join-Path $PSScriptRoot 'build\Release\version.dll'

if (-not (Test-Path (Join-Path $GameDir 'MCC-Win64-Shipping.exe'))) {
    Write-Host "MCC not found at $GameDir. Pass -GameDir." -ForegroundColor Yellow
    exit 1
}

if (Get-Process -Name 'MCC-Win64-Shipping*' -ErrorAction SilentlyContinue) {
    Write-Host 'Halo MCC is running. Close it first.' -ForegroundColor Yellow
    exit 1
}

if ($Uninstall) {
    if (Test-Path $Target) {
        Remove-Item $Target
        Write-Host 'HaloModMenu removed. Safe to launch with anti-cheat.' -ForegroundColor Green
    } else {
        Write-Host 'HaloModMenu was not installed.'
    }
    exit 0
}

if (-not (Test-Path $BuildDll)) {
    Write-Host "No build found. Run build.bat first." -ForegroundColor Yellow
    exit 1
}

Copy-Item $BuildDll $Target -Force
Write-Host "Installed to $Target" -ForegroundColor Green
Write-Host 'Launch MCC with anti-cheat DISABLED. Log: HaloModMenu.log in the same folder.'
