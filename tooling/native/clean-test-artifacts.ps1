# Dry run by default. Only known generated output is eligible; no arbitrary path input.
param([switch]$Apply, [string]$RunId)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'test-output.ps1')
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$runIdPattern = '^\d+-[a-f0-9]{32}$'
if ($RunId -and $RunId -notmatch $runIdPattern) {
    throw 'RunId must be one exact owned CTest run directory name.'
}
$lockPath = Join-Path $repoRoot '.agent\native-build.lock'
$lockStream = $null
try {
    if ($Apply) {
        $null = Assert-PikDrawOutputPath $lockPath $repoRoot
        New-Item -ItemType Directory -Path (Split-Path $lockPath -Parent) -Force | Out-Null
        try { $lockStream = [IO.File]::Open($lockPath, 'CreateNew', 'Write', 'Read') }
        catch [IO.IOException] { throw 'Build lock exists. Wait for its owner; cleanup will not remove the lock.' }
        $bytes = [Text.Encoding]::UTF8.GetBytes("pid=$PID source=$repoRoot cleanup=true")
        $lockStream.Write($bytes, 0, $bytes.Length)
        $lockStream.Flush()
    }
    $candidates = [Collections.Generic.List[IO.FileInfo]]::new()
    foreach ($relative in @('aqtinstall.log', 'out\audit-latest.json')) {
        $path = Assert-PikDrawOutputPath (Join-Path $repoRoot $relative) $repoRoot
        if (Test-Path -LiteralPath $path -PathType Leaf) { $candidates.Add((Get-Item -LiteralPath $path)) }
    }
    # The former E2E test wrote this exact basename in the source root.
    foreach ($item in Get-ChildItem -LiteralPath $repoRoot -File -Filter 'test_e2e_m1_workspace.*') {
        $candidates.Add($item)
    }
    foreach ($rule in @(
        @{ Directory = 'artifacts\evidence'; Filters = @('*.bmp', '*.png', '*.log') },
        @{ Directory = 'build\win-dev'; Filters = @('*.obj.tmp') },
        @{ Directory = 'build\win-dev\Testing\Temporary'; Filters = @('LastTest*.log', 'LastTestsFailed.log') }
    )) {
        $path = Assert-PikDrawOutputPath (Join-Path $repoRoot $rule.Directory) $repoRoot
        if (-not (Test-Path -LiteralPath $path -PathType Container)) { continue }
        # Explicit traversal, checking each node before descending.
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($path)
        while ($pending.Count -gt 0) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    throw "Refusing output through a reparse point: $($item.FullName)"
                }
                if ($item.PSIsContainer) {
                    $null = Assert-PikDrawOutputPath $item.FullName $repoRoot
                    $pending.Push($item.FullName)
                    continue
                }
                foreach ($filter in $rule.Filters) {
                    if ($item.Name -like $filter) { $candidates.Add($item); break }
                }
            }
        }
    }
    $runRoot = Assert-PikDrawOutputPath (Join-Path $repoRoot 'build\win-dev\test-output') $repoRoot
    $runs = @()
    if (Test-Path -LiteralPath $runRoot) {
        $runs = @(Get-ChildItem -LiteralPath $runRoot -Directory | Where-Object {
            $_.Name -match $runIdPattern -and
            (-not $RunId -or $_.Name -eq $RunId) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName '.pikdraw-test-run') -PathType Leaf)
        })
    }
    if ($RunId -and $runs.Count -ne 1) {
        throw "Expected exactly one owned CTest output directory for RunId $RunId."
    }
    if ($RunId) { $candidates.Clear() }
    $files = @($candidates | Sort-Object FullName -Unique)
    foreach ($file in $files) { $null = Assert-PikDrawOutputPath $file.FullName $repoRoot }
    $size = ($files | Measure-Object Length -Sum).Sum
    Write-Host "Generated files: $($files.Count); bytes: $size; owned CTest runs: $($runs.Count); apply: $Apply"
    if (-not $Apply) {
        $files | ForEach-Object { Write-Output $_.FullName }
        $runs | ForEach-Object { Write-Output $_.FullName }
    } else {
        foreach ($run in $runs) { Remove-PikDrawTestRun $run.FullName $repoRoot }
        foreach ($file in $files) { Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop }
    }
} finally {
    if ($null -ne $lockStream) {
        $lockStream.Dispose()
        Remove-Item -LiteralPath $lockPath -Force -ErrorAction Stop
    }
}
