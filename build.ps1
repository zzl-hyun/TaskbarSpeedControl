#!/usr/bin/env pwsh
# Build TaskbarSpeedControl
# Usage: .\build.ps1 [Debug|Release]

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    
    [ValidateSet("x64", "x86")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

# Find MSBuild using vswhere
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere not found at $vswhere"
    exit 1
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\MSBuild.exe" 2>$null | Select-Object -First 1

if (-not $msbuild -or -not (Test-Path $msbuild)) {
    Write-Error "MSBuild not found. Please install Visual Studio Build Tools with C++ workload."
    exit 1
}

Write-Host "Using MSBuild: $msbuild" -ForegroundColor Cyan

# Build C# app
Write-Host "`nBuilding C# app ($Configuration|$Platform)..." -ForegroundColor Yellow
& $msbuild src\TaskbarAutoHideSpeed\TaskbarAutoHideSpeed.csproj `
    /p:Configuration=$Configuration /p:Platform=$Platform `
    /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Error "C# build failed"
    exit 1
}

# Build native DLL
Write-Host "`nBuilding native DLL ($Configuration|$Platform)..." -ForegroundColor Yellow
& $msbuild src\TaskbarAutoHideHook\TaskbarAutoHideHook.vcxproj `
    /p:Configuration=$Configuration /p:Platform=$Platform `
    /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Error "Native DLL build failed"
    exit 1
}

# Copy DLL to app output
$dllSrc = "src\TaskbarAutoHideHook\$Platform\$Configuration\TaskbarAutoHideHook.dll"
$dllDst = "src\TaskbarAutoHideSpeed\bin\$Platform\$Configuration\net9.0-windows\TaskbarAutoHideHook.dll"

Write-Host "`nPreparing destination directory..." -ForegroundColor Yellow
$dstDir = Split-Path $dllDst
if (-not (Test-Path $dstDir)) {
    New-Item -ItemType Directory -Path $dstDir | Out-Null
}

Write-Host "`nAttempting graceful Explorer restart to replace DLL..." -ForegroundColor Yellow
$explorer = Get-Process -Name explorer -ErrorAction SilentlyContinue
if ($explorer) {
    Write-Host "Stopping Explorer..." -ForegroundColor Yellow
    try {
        Stop-Process -Name explorer -Force -ErrorAction Stop
        Start-Sleep -Seconds 1
    } catch {
        Write-Warning "Failed to stop Explorer: $_"
        Write-Warning "You may need to close Explorer manually before replacing the DLL."
    }
} else {
    Write-Host "Explorer not running, proceeding." -ForegroundColor Cyan
}

Write-Host "Copying native DLL..." -ForegroundColor Yellow
Copy-Item $dllSrc $dllDst -Force
Write-Host "Copied: $dllSrc -> $dllDst" -ForegroundColor Green

Write-Host "Restarting Explorer..." -ForegroundColor Yellow
Start-Process explorer.exe
Start-Sleep -Seconds 1

$exePath = "src\TaskbarAutoHideSpeed\bin\$Platform\$Configuration\net9.0-windows\TaskbarAutoHideSpeed.exe"
Write-Host "`n✓ Build complete!" -ForegroundColor Green
Write-Host "Executable: $exePath" -ForegroundColor Cyan
