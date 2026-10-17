param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateRange(3, 200)][int]$Iterations = 40,
    [ValidateSet('source_only', 'blur_single', 'shadow_single', 'blur_shadow_stack', 'many_blurred')]
    [string[]]$Scenarios = @('source_only', 'blur_single', 'shadow_single',
                             'blur_shadow_stack', 'many_blurred'),
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$executable = Join-Path $repoRoot 'build\win-dev\core\rendering\benchmark_pikdraw_rendering_effects.exe'

if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'tooling\native\build.ps1') -Configuration $Configuration -Test -TestTarget 'benchmark_pikdraw_rendering_effects' -BuildOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Effects benchmark build failed ($LASTEXITCODE)."
    }
}

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Benchmark executable was not found: $executable"
}

$cpu = try {
    (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name).Trim()
} catch {
    'unavailable'
}
Write-Output "host_cpu=$cpu"
Write-Output "configuration=$Configuration"
Write-Output "iterations=$Iterations"
Write-Output "scenarios=$($Scenarios -join ',')"

foreach ($scenario in $Scenarios) {
    & $executable --scenario $scenario --configuration $Configuration --iterations $Iterations
    if ($LASTEXITCODE -ne 0) {
        throw "Effects benchmark scenario '$scenario' failed ($LASTEXITCODE)."
    }
}
