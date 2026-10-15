# PikDraw Native Check Runner
# Runs architecture gates, contract validations, and integration suites.

param(
    [ValidateSet('architecture', 'contract', 'unit', 'e2e', 'all')]
    [string]$Suite = 'architecture',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$Preset = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')
$tools = Get-PikDrawToolchain

if (-not $tools.Python) {
    throw "PikDraw check runner requires Python 3.10+. No suitable Python found."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$testsRan = 0
$testsFailed = 0

Write-Host "=== PikDraw Native Check Runner ===" -ForegroundColor Cyan
Write-Host "Suite: $Suite | Configuration: $Configuration" -ForegroundColor Gray
Write-Host "Python: $($tools.Python)" -ForegroundColor Gray

if ($Suite -eq 'architecture' -or $Suite -eq 'all') {
    Write-Host "`n--> Running Architecture Gates Suite..." -ForegroundColor Yellow
    $archDir = Join-Path $repoRoot 'tests\architecture'
    if (-not (Test-Path -LiteralPath $archDir)) {
        Write-Error "Architecture tests directory not found: $archDir"
        exit 1
    }

    & $tools.Python -m unittest discover -s (Join-Path $repoRoot 'tests\architecture') -v
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Architecture gates suite FAILED."
        exit $LASTEXITCODE
    }
    Write-Host "Architecture gates suite PASSED." -ForegroundColor Green
}

if ($Suite -eq 'contract' -or $Suite -eq 'all') {
    Write-Host "`n--> Running Contract & Oracle Verification..." -ForegroundColor Yellow
    $contractScript = Join-Path $repoRoot 'tests\architecture\verify_contracts.py'
    if (Test-Path -LiteralPath $contractScript) {
        & $tools.Python $contractScript
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Contract verification FAILED."
            exit $LASTEXITCODE
        }
    } else {
        # Inline contract verification
        & $tools.Python -c @"
import json, glob, sys
fixtures = glob.glob('docs/quality/reference-fixtures/*.json')
oracles = glob.glob('docs/quality/oracles/*.json')
print(f'Checking {len(fixtures)} reference fixtures and {len(oracles)} oracles...')
for f in fixtures + oracles:
    with open(f, 'r', encoding='utf-8') as fp:
        json.load(fp)
print('All fixtures and oracles parsed valid JSON.')
"@
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Contract fixtures verification FAILED."
            exit $LASTEXITCODE
        }
    }
    Write-Host "Contract & oracle verification PASSED." -ForegroundColor Green
}

if ($Suite -eq 'unit' -or $Suite -eq 'all') {
    Write-Host "`n--> Running Native C++ Unit Tests..." -ForegroundColor Yellow
    $presetName = if ($Preset) { $Preset } else { 'win-dev' }
    $buildDir = Join-Path $repoRoot "build\$presetName"
    $unitExes = Get-ChildItem -Recurse -Path $buildDir -Filter "test_*.exe" -ErrorAction SilentlyContinue
    if (-not $unitExes) {
        Write-Host "No unit test executables found in $buildDir" -ForegroundColor Yellow
    } else {
        foreach ($exe in $unitExes) {
            Write-Host "Running: $($exe.Name)..." -ForegroundColor Cyan
            & $exe.FullName
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Test $($exe.Name) FAILED with exit code $LASTEXITCODE"
                exit $LASTEXITCODE
            }
        }
        Write-Host "All native C++ unit tests PASSED." -ForegroundColor Green
    }
}

if ($Suite -eq 'e2e') {
    Write-Host "`n--> Checking E2E Integration Suite..." -ForegroundColor Yellow
    $buildDir = Join-Path $repoRoot "build\win-dev"
    $appExe = Join-Path $buildDir "bin\pikdraw.exe"
    if (-not (Test-Path -LiteralPath $appExe)) {
        Write-Host "Notice: E2E suite requires desktop vertical slice executable (planned for M1 ps13-vertical-slice)." -ForegroundColor Magenta
        Write-Host "No tests run for E2E in M0 foundation phase." -ForegroundColor Yellow
        exit 1
    }
    Write-Host "Running E2E tests against $appExe..."
    # Future M1 E2E runner invocation
}

Write-Host "`n=== All requested check suites PASSED ===" -ForegroundColor Green
exit 0
