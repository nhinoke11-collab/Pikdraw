# PikDraw Native Developer Runner
# Launches the Qt shell preview or full editor across configurations.

param(
    [ValidateSet('Preview', 'Editor')]
    [string]$Mode = 'Preview',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$Scenario = 'basic',

    [switch]$ForceSoftware,

    [switch]$Build,

    [switch]$Clean,

    [string]$Target = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')
$tools = Get-PikDrawToolchain

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$presetName = if ($Configuration -eq 'Release') { 'win-msvc-release' } else { 'win-dev' }
$buildDir = Join-Path $repoRoot "build\$presetName"

Write-Host "=== PikDraw Native Developer Runner ===" -ForegroundColor Cyan
Write-Host "Mode: $Mode | Configuration: $Configuration | Scenario: $Scenario" -ForegroundColor Gray
if ($ForceSoftware) {
    Write-Host "Graphics: Forced Software 2D Fallback" -ForegroundColor Yellow
}

# Optional Clean
if ($Clean) {
    if (Test-Path -LiteralPath $buildDir) {
        Write-Host "Cleaning build directory: $buildDir" -ForegroundColor Yellow
        Remove-Item -Recurse -Force -LiteralPath $buildDir
    }
}

# Optional Build
if ($Build) {
    if (-not $tools.CMake) {
        throw "Cannot build: cmake executable was not found."
    }
    Write-Host "Configuring and building preset '$presetName'..." -ForegroundColor Yellow
    & $tools.CMake --preset $presetName
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $buildArgs = @("--build", "--preset", $presetName)
    if ($Target) {
        $buildArgs += @("--target", $Target)
    }
    & $tools.CMake @buildArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Target) {
        exit 0
    }
}

# Locate Desktop executable
$candidateExes = @(
    (Join-Path $buildDir "bin\pikdraw-desktop.exe"),
    (Join-Path $buildDir "bin\pikdraw.exe"),
    (Join-Path $buildDir "apps\desktop\pikdraw-desktop.exe"),
    (Join-Path $buildDir "apps\desktop\pikdraw.exe")
)

$appExe = $null
foreach ($cand in $candidateExes) {
    if (Test-Path -LiteralPath $cand -PathType Leaf) {
        $appExe = $cand
        break
    }
}

if (-not $appExe) {
    Write-Host "Error: Native executable not found in $buildDir." -ForegroundColor Red
    Write-Host "In Milestone M0 (Foundation Lock), the C++ desktop shell is not yet compiled." -ForegroundColor Yellow
    Write-Host "Desktop preview will be available in Milestone M1 (ps05-native-preview / ps13-vertical-slice)." -ForegroundColor Yellow
    Write-Host "Use 'cmake --preset win-dev' and 'cmake --build --preset win-dev' once native targets are registered." -ForegroundColor Gray
    exit 1
}

# Launch executable with arguments
$exeArgs = @(
    "--mode", $Mode.ToLower(),
    "--scenario", $Scenario
)
if ($ForceSoftware) {
    $exeArgs += "--force-software"
}

Write-Host "Launching $appExe $($exeArgs -join ' ')..." -ForegroundColor Green
& $appExe @exeArgs
exit $LASTEXITCODE
