# Installs HaloModMenu in MCC's win64 folder as version.dll.
#
#   Right-click this file and choose "Run with PowerShell"
#   .\install.ps1 -GameDir "<MCC>\mcc\binaries\win64"   if MCC isn't found automatically
#   .\install.ps1 -Uninstall   same as running uninstall.ps1 (do this before playing with anti-cheat on)
#
# Launch MCC with "Play Halo: MCC (Anti-Cheat Disabled)" for the mod to load.

param(
    [switch]$Uninstall,
    [string]$GameDir,
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'

# Removing is uninstall.ps1's job (it checks the file really is HaloModMenu).
if ($Uninstall) {
    & (Join-Path $PSScriptRoot 'uninstall.ps1') -GameDir $GameDir -NoPause:$NoPause
    exit $LASTEXITCODE
}

# Keeps the window open when the script was started by right-click > Run with PowerShell.
function Finish([int]$code) {
    if (-not $NoPause) { try { Read-Host 'Press Enter to close' | Out-Null } catch {} }
    exit $code
}

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

try {
    if (-not $GameDir) { $GameDir = Find-GameDir }
    if (-not $GameDir) {
        Write-Host 'Could not find MCC through Steam. Run this again with -GameDir "<MCC>\mcc\binaries\win64".' -ForegroundColor Yellow
        Finish 1
    }

    # Release zips have version.dll next to this script; a source checkout has it in build\Release.
    $Target   = Join-Path $GameDir 'version.dll'
    $BuildDll = @((Join-Path $PSScriptRoot 'version.dll'), (Join-Path $PSScriptRoot 'build\Release\version.dll')) |
        Where-Object { Test-Path $_ } | Select-Object -First 1

    if (-not (Test-Path (Join-Path $GameDir 'MCC-Win64-Shipping.exe'))) {
        Write-Host "MCC not found at $GameDir. Pass -GameDir." -ForegroundColor Yellow
        Finish 1
    }

    if (Get-Process -Name 'MCC-Win64-Shipping*' -ErrorAction SilentlyContinue) {
        Write-Host 'Halo MCC is running. Close it first, then run this again.' -ForegroundColor Yellow
        Finish 1
    }

    if (-not $BuildDll) {
        Write-Host 'No version.dll found. Run build.bat first (or run this from an unzipped release).' -ForegroundColor Yellow
        Finish 1
    }

    Copy-Item $BuildDll $Target -Force
    Write-Host "Installed to $Target" -ForegroundColor Green
    Write-Host 'Launch MCC with anti-cheat DISABLED (Play Halo: MCC (Anti-Cheat Disabled)).'
    Write-Host 'If it is gone later, Windows Defender removed it; see the README.'
    Finish 0
} catch {
    Write-Host "Something went wrong: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'If Windows Defender deleted version.dll, restore it from Protection history and try again.'
    Finish 1
}
