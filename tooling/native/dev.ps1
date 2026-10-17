# Launch the one shared native application.
param(
    [ValidateSet('Preview', 'Editor')][string]$Mode = 'Editor',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$Scenario = 'basic',
    [switch]$ForceSoftware,
    [switch]$Build,
    [switch]$Clean,
    [string]$Target = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Assert-PikDrawPrimarySource $repoRoot
if ($Clean -and -not $Build) { throw '-Clean requires -Build.' }
if ($Build) {
    $buildArgs = @{ Configuration = $Configuration; Clean = $Clean }
    if ($Target) { $buildArgs.Target = @($Target) }
    & (Join-Path $PSScriptRoot 'build.ps1') @buildArgs
    if ($Target) { return }
}
$appExe = Join-Path $repoRoot 'build\win-dev\apps\desktop\pikdraw.exe'
if (-not (Test-Path -LiteralPath $appExe -PathType Leaf)) {
    throw 'Canonical pikdraw.exe is missing. Run tooling/native/build.ps1.'
}
$exeArgs = @('--mode', $Mode.ToLower(), '--scenario', $Scenario)
if ($ForceSoftware) { $exeArgs += '--force-software' }
& $appExe @exeArgs
if ($LASTEXITCODE -ne 0) { throw "PikDraw exited with code $LASTEXITCODE." }
