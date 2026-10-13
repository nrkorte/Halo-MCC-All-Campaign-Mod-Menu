# Installs (or removes) HaloModMenu in MCC's win64 folder as version.dll.
#
#   .\install.ps1              copy version.dll into the game (found through Steam)
#   .\install.ps1 -Uninstall   remove it (do this before playing with anti-cheat on)
#   .\install.ps1 -GameDir "<MCC>\mcc\binaries\win64"   if MCC isn't found automatically
#
# Launch MCC with "Play Halo: MCC (Anti-Cheat Disabled)" for the mod to load.

param(
    [switch]$Uninstall,
    [string]$GameDir
)

$ErrorActionPreference = 'Stop'

# Finds MCC through Steam's install path (registry) and every library in libraryfolders.vdf.
function Find-GameDir {
    $relative = 'steamapps\common\Halo The Master Chief Collection\mcc\binaries\win64'
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return $null }
    $libraries = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        $libraries += [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"') |
            ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' }
    }
    foreach ($library in $libraries) {
        $dir = Join-Path $library $relative
        if (Test-Path (Join-Path $dir 'MCC-Win64-Shipping.exe')) { return $dir }
    }
    return $null
}

if (-not $GameDir) { $GameDir = Find-GameDir }
if (-not $GameDir) {
    Write-Host 'Could not find MCC through Steam. Pass -GameDir "<MCC>\mcc\binaries\win64".' -ForegroundColor Yellow
    exit 1
}

# Release zips have version.dll next to this script; a source checkout has it in build\Release.
$Target   = Join-Path $GameDir 'version.dll'
$BuildDll = @((Join-Path $PSScriptRoot 'version.dll'), (Join-Path $PSScriptRoot 'build\Release\version.dll')) |
    Where-Object { Test-Path $_ } | Select-Object -First 1

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

if (-not $BuildDll) {
    Write-Host "No version.dll found. Run build.bat first (or run this from an unzipped release)." -ForegroundColor Yellow
    exit 1
}

Copy-Item $BuildDll $Target -Force
Write-Host "Installed to $Target" -ForegroundColor Green
Write-Host 'Launch MCC with anti-cheat DISABLED. Log: HaloModMenu.log in the same folder.'
