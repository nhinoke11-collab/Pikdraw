# Forward every argument to the provider-neutral Python CLI.
# Uses an explicit override, a normal Python installation, or the local bundled runtime.
$ErrorActionPreference = 'Stop'
$PikDrawCandidates = @()
if ($env:PIKDRAW_PYTHON) {
    $PikDrawCandidates += $env:PIKDRAW_PYTHON
}
foreach ($PikDrawCommand in @(Get-Command python3,python -CommandType Application -ErrorAction SilentlyContinue)) {
    if ($PikDrawCommand.Source -notmatch '[\\/]WindowsApps[\\/]') {
        $PikDrawCandidates += $PikDrawCommand.Source
    }
}
if ($env:USERPROFILE) {
    $PikDrawCandidates += Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
}
$PikDrawExecutable = $null
foreach ($PikDrawCandidate in @($PikDrawCandidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $PikDrawCandidate -PathType Leaf)) { continue }
    try {
        & $PikDrawCandidate -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)'
        if ($LASTEXITCODE -eq 0) {
            $PikDrawExecutable = $PikDrawCandidate
            break
        }
    } catch {
        continue
    }
}
if (-not $PikDrawExecutable) {
    throw 'PikDraw needs Python 3.10+. Install Python or set PIKDRAW_PYTHON to its executable path.'
}
$PikDrawScript = Join-Path $PSScriptRoot 'agent_harness.py'
& $PikDrawExecutable $PikDrawScript @args
exit $LASTEXITCODE
