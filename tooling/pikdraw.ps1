# PikDraw Streamlined Developer Tooling CLI
# Direct, fast native development runner for PikDraw.
# Replaces multi-layer harness bureaucracy with direct, developer-friendly commands.

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'native/forward.ps1')

$Command = if ($args.Count -gt 0) { [string]$args[0] } else { 'help' }
$RestArgs = @($args | Select-Object -Skip 1)

switch ($Command.ToLowerInvariant()) {
    'doctor' {
        . (Join-Path $PSScriptRoot 'native/env.ps1')
        $tools = Get-PikDrawToolchain
        $issues = @()
        if (-not $tools.Python) { $issues += "Python 3.10+ not found" }
        if (-not $tools.CMake)  { $issues += "CMake not found" }
        if (-not $tools.Ninja)  { $issues += "Ninja not found" }
        if (-not $tools.Clang -and -not $tools.MSVC) { $issues += "Neither Clang nor MSVC found" }

        $moduleCount = 0
        $registryPath = Join-Path $repoRoot 'registry/modules.json'
        if (Test-Path -LiteralPath $registryPath) {
            try {
                $reg = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
                $moduleCount = $reg.modules.Count
            } catch {
                $issues += "Cannot parse registry/modules.json"
            }
        } else {
            $issues += "registry/modules.json not found"
        }

        $statePath = Join-Path $repoRoot 'docs/project-state.json'
        if (Test-Path -LiteralPath $statePath) {
            $stateSize = (Get-Item -LiteralPath $statePath).Length
            if ($stateSize -gt 8000) {
                $issues += "docs/project-state.json exceeds 8000 bytes ($stateSize bytes)"
            }
        }

        $appExe = Join-Path $repoRoot 'build\win-dev\apps\desktop\pikdraw.exe'

        $result = [ordered]@{
            ok             = ($issues.Count -eq 0)
            product        = "PikDraw"
            schema_version = 1
            modules        = $moduleCount
            issues         = $issues
            workflow       = "single-source"
            agent_system   = "shared AGENTS workflow"
            source_root    = $repoRoot
            app_executable = $appExe
        }
        $result | ConvertTo-Json -Depth 4
        if ($issues.Count -gt 0) { exit 1 }
        exit 0
    }

    'build' {
        Invoke-PikDrawNative (Join-Path $PSScriptRoot 'native/build.ps1') $RestArgs
        exit $LASTEXITCODE
    }

    'test' {
        Invoke-PikDrawNative (Join-Path $PSScriptRoot 'native/build.ps1') $RestArgs @{ Test = $true }
        exit $LASTEXITCODE
    }

    'check' {
        Invoke-PikDrawNative (Join-Path $PSScriptRoot 'native/check.ps1') $RestArgs
        exit $LASTEXITCODE
    }

    'package' {
        Invoke-PikDrawNative (Join-Path $PSScriptRoot 'native/package.ps1') $RestArgs
        exit $LASTEXITCODE
    }

    'run' {
        $appExe = Join-Path $repoRoot 'build\win-dev\apps\desktop\pikdraw.exe'
        if (-not (Test-Path -LiteralPath $appExe)) {
            Write-Host "App executable not found at $appExe. Building first..." -ForegroundColor Yellow
            & (Join-Path $PSScriptRoot 'native/build.ps1')
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
        & $appExe @RestArgs
        exit $LASTEXITCODE
    }

    'task' {
        throw 'Legacy task commands were removed. Work directly per AGENTS.md; use build -Status for build coordination.'
    }

    'context' {
        throw 'Legacy context packets were removed. Read AGENTS.md and docs/START.md.'
    }

    default {
        Write-Host "=== PikDraw Streamlined Tooling CLI ===" -ForegroundColor Cyan
        Write-Host "Usage: .\tooling\pikdraw.ps1 <command> [options]" -ForegroundColor White
        Write-Host ""
        Write-Host "Commands:" -ForegroundColor Yellow
        Write-Host "  doctor       Check toolchain, environment, and repository health"
        Write-Host "  build        Build native desktop application and libraries"
        Write-Host "  test         Build/run selected tests (-TestTarget), full suite requires -AllTests"
        Write-Host "  check        Run architecture gates and contract suites (check.ps1)"
        Write-Host "  run          Launch the built PikDraw desktop executable"
        Write-Host "  package      Package release desktop, sidecar, and COM server"
        Write-Host ""
        Write-Host "Examples:" -ForegroundColor Gray
        Write-Host "  .\tooling\pikdraw.ps1 doctor"
        Write-Host "  .\tooling\pikdraw.ps1 build"
        Write-Host "  .\tooling\pikdraw.ps1 build -Status"
        Write-Host "  .\tooling\pikdraw.ps1 test -Submit -TestTarget test_pikdraw_document_storage"
        Write-Host "  .\tooling\pikdraw.ps1 check -Suite architecture"
        Write-Host "  .\tooling\pikdraw.ps1 run"
        exit 0
    }
}
