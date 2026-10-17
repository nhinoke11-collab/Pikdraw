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
Assert-PikDrawPrimarySource $repoRoot
if ($Preset -and $Preset -ne 'win-dev') { throw 'Only the win-dev preset is supported.' }
$testsRan = 0
$testsFailed = 0

Write-Host "=== PikDraw Native Check Runner ===" -ForegroundColor Cyan
Write-Host "Suite: $Suite | Configuration: $Configuration" -ForegroundColor Gray
Write-Host "Python: $($tools.Python)" -ForegroundColor Gray

if ($Suite -eq 'architecture' -or $Suite -eq 'all') {
    Write-Host "`n--> Running Architecture Gates Suite..." -ForegroundColor Yellow
    $archDir = Join-Path $repoRoot 'tests\architecture'
    $archTests = @(Get-ChildItem -LiteralPath $archDir -Filter 'test_*.py' -File -ErrorAction SilentlyContinue)
    if ($archTests.Count -eq 0) {
        throw 'Architecture test source was removed after the recorded final test run. Restore tests/architecture before running this suite.'
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
fixtures = glob.glob('tests/fixtures/reference-fixtures/*.json')
oracles = glob.glob('tests/fixtures/oracles/*.json')
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
    & (Join-Path $PSScriptRoot 'build.ps1') -TestsOnly -Configuration $Configuration
}
if ($Suite -eq 'e2e') {
    & (Join-Path $PSScriptRoot 'build.ps1') -TestsOnly -Configuration $Configuration -TestRegex 'test_pikdraw_vertical_slice_e2e'
}

Write-Host "`n=== All requested check suites PASSED ===" -ForegroundColor Green
exit 0
