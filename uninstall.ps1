# Removes HaloModMenu from Halo MCC.
#
#   Right-click this file and choose "Run with PowerShell"
#   .\uninstall.ps1 -GameDir "<MCC>\mcc\binaries\win64"   if MCC isn't found automatically
#   .\uninstall.ps1 -RemoveSettings                       also delete the saved settings and logs
#
# It only deletes a version.dll that really is HaloModMenu, so another mod's
# version.dll is left alone. Do this before playing online with anti-cheat on.

param(
    [string]$GameDir,
    [switch]$RemoveSettings,
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'

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

# HaloModMenu's DLL contains the wide string "HaloModMenu.log" (its log file name).
function Test-IsHaloModMenu([string]$path) {
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $marker = ('HaloModMenu.log'.ToCharArray() -join "`0")
    return $latin1.GetString([System.IO.File]::ReadAllBytes($path)).Contains($marker)
}

try {
    if (-not $GameDir) { $GameDir = Find-GameDir }
    if (-not $GameDir) {
        Write-Host 'Could not find MCC through Steam. Run this again with -GameDir "<MCC>\mcc\binaries\win64".' -ForegroundColor Yellow
        Finish 1
    }
    if (-not (Test-Path (Join-Path $GameDir 'MCC-Win64-Shipping.exe'))) {
        Write-Host "MCC not found at $GameDir. Pass -GameDir." -ForegroundColor Yellow
        Finish 1
    }
    if (Get-Process -Name 'MCC-Win64-Shipping*' -ErrorAction SilentlyContinue) {
        Write-Host 'Halo MCC is running. Close it first, then run this again.' -ForegroundColor Yellow
        Finish 1
    }

    Write-Host "MCC folder: $GameDir"
    $removed = $false
    foreach ($name in 'version.dll', 'version.dll.disabled') {
        $file = Join-Path $GameDir $name
        if (-not (Test-Path $file)) { continue }
        if (Test-IsHaloModMenu $file) {
            Remove-Item $file -Force
            Write-Host "Removed $name" -ForegroundColor Green
            $removed = $true
        } else {
            Write-Host "$name isn't HaloModMenu, so it was left alone." -ForegroundColor Yellow
        }
    }
    if (-not $removed) { Write-Host 'HaloModMenu was not installed.' }

    $extras = 'HaloModMenu.ini', 'HaloModMenu.log', 'HaloModMenu.prev.log' |
        Where-Object { Test-Path (Join-Path $GameDir $_) }
    if ($extras) {
        $remove = $RemoveSettings
        if (-not $remove -and -not $NoPause) {
            $answer = Read-Host 'Also delete your saved settings and logs? (y/N)'
            $remove = $answer -match '^[yY]'
        }
        if ($remove) {
            foreach ($name in $extras) { Remove-Item (Join-Path $GameDir $name) -Force }
            Write-Host 'Settings and logs deleted.' -ForegroundColor Green
        } else {
            Write-Host 'Saved settings kept (HaloModMenu.ini), in case you reinstall.'
        }
    }

    if ($removed) { Write-Host 'Done. It is safe to launch MCC with anti-cheat.' -ForegroundColor Green }
    Finish 0
} catch {
    Write-Host "Something went wrong: $($_.Exception.Message)" -ForegroundColor Red
    Finish 1
}
