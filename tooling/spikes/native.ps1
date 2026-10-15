[CmdletBinding()]
param(
  [Parameter(Mandatory)][ValidateSet('A','B','C')][string]$Lane,
  [Parameter(Mandatory)][string]$SourceRoot,
  [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
  [ValidateSet('Root','RenderMath')][string]$EntryPoint = 'Root',
  [ValidateSet('Configure','Build','Test','All')][string]$Mode = 'All'
)
$ErrorActionPreference = 'Stop'
$spikeBuildRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$spikeLaneRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$spikeCommonGit = (& git -C $spikeBuildRoot rev-parse --path-format=absolute --git-common-dir).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve repository toolchain cache.' }
$spikeCache = Join-Path (Split-Path -Parent $spikeCommonGit) '.agent/toolchains'
$spikeCompilerDir = Join-Path $spikeCache 'llvm-mingw-20261006-ucrt-x86_64/bin'
$spikeCompiler = Join-Path $spikeCompilerDir 'clang++.exe'
$spikeCMakeDir = Join-Path $spikeCache 'python-tools/cmake/data/bin'
$spikeCMake = Join-Path $spikeCMakeDir 'cmake.exe'
$spikeCTest = Join-Path $spikeCMakeDir 'ctest.exe'
$spikeNinja = Join-Path $spikeCache 'python-tools/bin/ninja.exe'
foreach ($spikeTool in @($spikeCompiler,$spikeCMake,$spikeCTest,$spikeNinja)) {
  if (-not (Test-Path -LiteralPath $spikeTool -PathType Leaf)) { throw "Missing pinned native tool: $spikeTool" }
}
$env:PATH = $spikeCompilerDir + ';' + (Split-Path -Parent $spikeNinja) + ';' + $spikeCMakeDir + ';' + $env:PATH
$spikeOutput = Join-Path $spikeLaneRoot ("out/spikes/" + $Lane.ToLowerInvariant() + '-' + $Configuration.ToLowerInvariant() + '-' + $EntryPoint.ToLowerInvariant())
$spikeSource = $spikeBuildRoot
if ($EntryPoint -eq 'RenderMath') {
  if ($Lane -ne 'A') { throw 'RenderMath is only for A correctness tests, not viewport acceptance.' }
  $spikeSource = Join-Path $spikeLaneRoot 'core/rendering'
}
if ($Mode -in @('Configure','All')) {
  $spikeConfigureArgs = @('-S',$spikeSource,'-B',$spikeOutput,'-G','Ninja',
    "-DCMAKE_BUILD_TYPE=$Configuration","-DCMAKE_CXX_COMPILER=$spikeCompiler",
    "-DCMAKE_MAKE_PROGRAM=$spikeNinja","-DPIKDRAW_SPIKE_SOURCE_ROOT=$spikeLaneRoot")
  if ($EntryPoint -eq 'Root') { $spikeConfigureArgs += "-DPIKDRAW_SPIKE_${Lane}=ON" }
  if ($Lane -in @('B','C')) { $spikeConfigureArgs += '-DCMAKE_EXE_LINKER_FLAGS=-static' }
  & $spikeCMake @spikeConfigureArgs
  if ($LASTEXITCODE -ne 0) { throw 'Native configure failed.' }
}
if ($Mode -in @('Build','All')) {
  & $spikeCMake --build $spikeOutput --parallel 2
  if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
}
if ($Mode -in @('Test','All')) {
  $spikeDiscoveryText = & $spikeCTest --test-dir $spikeOutput --show-only=json-v1
  if ($LASTEXITCODE -ne 0) { throw 'Native test discovery failed.' }
  $spikeDiscovery = $spikeDiscoveryText | ConvertFrom-Json
  if (@($spikeDiscovery.tests).Count -eq 0) { throw 'No native tests discovered; not a pass.' }
  & $spikeCTest --test-dir $spikeOutput --output-on-failure
  if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
}
