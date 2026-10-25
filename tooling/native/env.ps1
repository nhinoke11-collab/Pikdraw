# PikDraw Native Environment Discovery Helper
# Finds Python, CMake, Ninja, MSVC, and Clang runtimes across system and repo toolchains.

$ErrorActionPreference = 'Stop'

function Assert-PikDrawPrimarySource([string]$SourceRoot) {
    $sourceGit = Get-Item -Force -LiteralPath (Join-Path $SourceRoot '.git')
    if (-not $sourceGit.PSIsContainer) {
        throw 'Single-source policy: run native tooling from the primary PikDraw source.'
    }
    $currentTop = git rev-parse --show-toplevel 2>$null
    if ($LASTEXITCODE -eq 0 -and $currentTop) {
        $currentGit = Get-Item -Force -LiteralPath (Join-Path $currentTop.Trim() '.git')
        if (-not $currentGit.PSIsContainer) {
            throw 'Leave the legacy worktree and run from the primary PikDraw source.'
        }
    }
}

function Get-PikDrawToolchain {
    param([switch]$PythonOnly)
    $tools = @{
        Python = $null
        CMake = $null
        Ninja = $null
        Clang = $null
        MSVC = $null
        RepoRoot = $null
    }

    # Determine true repository root (handles both primary clone and worktrees)
    try {
        $gitCommon = (git rev-parse --git-common-dir 2>$null).Trim()
        if ($gitCommon) {
            $tools.RepoRoot = (Resolve-Path (Join-Path $gitCommon "..")).Path
        }
    } catch {}
    if (-not $tools.RepoRoot) {
        $tools.RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    }

    $toolchainRoot = Join-Path $tools.RepoRoot '.agent\toolchains'

    # 1. Discover Python (>= 3.10)
    $pythonCandidates = @()
    if ($env:PIKDRAW_PYTHON) { $pythonCandidates += $env:PIKDRAW_PYTHON }
    foreach ($cmd in @(Get-Command python3,python -CommandType Application -ErrorAction SilentlyContinue)) {
        if ($cmd.Source -notmatch '[\\/]WindowsApps[\\/]') {
            $pythonCandidates += $cmd.Source
        }
    }
    if ($env:USERPROFILE) {
        $pythonCandidates += Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    }
    foreach ($cand in ($pythonCandidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $cand -PathType Leaf) {
            try {
                & $cand -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)'
                if ($LASTEXITCODE -eq 0) {
                    $tools.Python = $cand
                    break
                }
            } catch {}
        }
    }

    if ($PythonOnly) { return $tools }

    # 2. Discover CMake
    $cmakeCandidates = @()
    foreach ($cmd in @(Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue)) {
        $cmakeCandidates += $cmd.Source
    }
    $cmakeCandidates += Join-Path $toolchainRoot 'python-tools\cmake\data\bin\cmake.exe'
    $cmakeCandidates += Join-Path $toolchainRoot 'python-tools\bin\cmake.exe'
    $cmakeCandidates += 'C:\Program Files\CMake\bin\cmake.exe'
    foreach ($cand in ($cmakeCandidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $cand -PathType Leaf) {
            try {
                & $cand --version | Out-Null
                if ($LASTEXITCODE -eq 0) {
                    $tools.CMake = $cand
                    break
                }
            } catch {}
        }
    }

    # 3. Discover Ninja
    $ninjaCandidates = @()
    foreach ($cmd in @(Get-Command ninja -CommandType Application -ErrorAction SilentlyContinue)) {
        $ninjaCandidates += $cmd.Source
    }
    $ninjaCandidates += Join-Path $toolchainRoot 'python-tools\bin\ninja.exe'
    $ninjaCandidates += 'C:\Program Files\Ninja\ninja.exe'
    foreach ($cand in ($ninjaCandidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $cand -PathType Leaf) {
            try {
                & $cand --version | Out-Null
                if ($LASTEXITCODE -eq 0) {
                    $tools.Ninja = $cand
                    break
                }
            } catch {}
        }
    }

    # 4. Discover Clang++
    $clangCandidates = @()
    foreach ($cmd in @(Get-Command clang++ -CommandType Application -ErrorAction SilentlyContinue)) {
        $clangCandidates += $cmd.Source
    }
    $clangCandidates += Join-Path $toolchainRoot 'llvm-mingw-20261006-ucrt-x86_64\bin\clang++.exe'
    foreach ($cand in ($clangCandidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $cand -PathType Leaf) {
            try {
                & $cand --version | Out-Null
                if ($LASTEXITCODE -eq 0) {
                    $tools.Clang = $cand
                    break
                }
            } catch {}
        }
    }

    # 5. Discover MSVC
    $msvcCandidates = @()
    $msvcCandidates += 'C:\pd-vs\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
    foreach ($cmd in @(Get-Command cl -CommandType Application -ErrorAction SilentlyContinue)) {
        $msvcCandidates += $cmd.Source
    }
    foreach ($cand in ($msvcCandidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $cand -PathType Leaf) {
            $tools.MSVC = $cand
            break
        }
    }

    # Prepend discovered tool directories to PATH so CMake can locate ninja, clang++, etc.
    $pathsToAdd = @()
    if ($tools.Ninja) { $pathsToAdd += (Split-Path $tools.Ninja -Parent) }
    if ($tools.Clang) { $pathsToAdd += (Split-Path $tools.Clang -Parent) }
    if ($tools.CMake) { $pathsToAdd += (Split-Path $tools.CMake -Parent) }
    if ($tools.Python) { $pathsToAdd += (Split-Path $tools.Python -Parent) }
    foreach ($p in ($pathsToAdd | Select-Object -Unique)) {
        if ($env:PATH -notlike "*$p*") {
            $env:PATH = "$p;$env:PATH"
        }
    }

    return $tools
}
