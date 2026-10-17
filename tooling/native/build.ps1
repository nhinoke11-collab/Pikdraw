# The only local configure/build/test entrypoint. All sessions share one lock/output.
# Default builds stay incremental; only explicit -Clean clears targets in this build tree.
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string[]]$Target = @('pikdraw'),
    [switch]$Clean,
    [switch]$Test,
    [switch]$TestsOnly,
    [string[]]$TestTarget = @(),
    [switch]$BuildOnly,
    [string]$TestRegex = ''
)
$ErrorActionPreference = 'Stop'
if ($TestTarget.Count -gt 0 -and -not $Test) {
    throw '-TestTarget requires -Test so tests are configured and the shared build lock is held.'
}
if ($BuildOnly -and (-not $Test -or $TestTarget.Count -eq 0)) {
    throw '-BuildOnly requires -Test and at least one explicit -TestTarget.'
}
if ($TestsOnly -and ($BuildOnly -or $TestTarget.Count -gt 0)) {
    throw '-TestsOnly only runs CTest; do not combine it with build-target options.'
}
. (Join-Path $PSScriptRoot 'env.ps1')
$tools = Get-PikDrawToolchain
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Assert-PikDrawPrimarySource $repoRoot
$buildDir = Join-Path $repoRoot 'build\win-dev'
$boardDir = Join-Path $repoRoot '.agent'
New-Item -ItemType Directory -Path $boardDir -Force | Out-Null
$lockPath = Join-Path $boardDir 'native-build.lock'
$lockStream = $null
try {
    try {
        $lockStream = [System.IO.File]::Open($lockPath, 'CreateNew', 'Write', 'None')
    } catch [System.IO.IOException] {
        throw 'Another session owns .agent/native-build.lock. Wait for it; never steal an active build lock.'
    }
    $lockBytes = [System.Text.Encoding]::UTF8.GetBytes("pid=$PID source=$repoRoot")
    $lockStream.Write($lockBytes, 0, $lockBytes.Length)
    $lockStream.Flush()
    Push-Location -LiteralPath $repoRoot
    try {
        if (-not $tools.CMake) { throw 'CMake was not found.' }
        if ($Test -or $TestsOnly) {
            $testSourceRoots = @(
                (Join-Path $repoRoot 'apps\desktop\tests'),
                (Join-Path $repoRoot 'core\color\tests'),
                (Join-Path $repoRoot 'core\document\tests'),
                (Join-Path $repoRoot 'core\geometry\tests'),
                (Join-Path $repoRoot 'core\rendering\tests'),
                (Join-Path $repoRoot 'core\typography\tests'),
                (Join-Path $repoRoot 'modules\interop\tests'),
                (Join-Path $repoRoot 'modules\vector\tests'),
                (Join-Path $repoRoot 'modules\automation\tests'),
                (Join-Path $repoRoot 'apps\mcp-server\tests'),
                (Join-Path $repoRoot 'platform\workers\tests'),
                (Join-Path $repoRoot 'tests\integration'),
                (Join-Path $repoRoot 'tests\architecture'),
                (Join-Path $repoRoot 'tests\harness')
            )
            $testSources = @(
                foreach ($testRoot in $testSourceRoots) {
                    if (Test-Path -LiteralPath $testRoot -PathType Container) {
                        Get-ChildItem -LiteralPath $testRoot -Recurse -File -ErrorAction SilentlyContinue |
                            Where-Object { $_.Extension -in @('.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.m', '.mm', '.py', '.rs') }
                    }
                }
            )
            if ($testSources.Count -eq 0) {
                throw 'Test source code was removed after the recorded final CTest run. Restore test sources before requesting -Test or -TestsOnly.'
            }
        }
        if (-not $TestsOnly) {
            $configureArgs = @('--preset', 'win-dev', "-DCMAKE_BUILD_TYPE=$Configuration")
            if ($Test) { $configureArgs += '-DPIKDRAW_BUILD_TESTS=ON' }
            & $tools.CMake @configureArgs
            if ($LASTEXITCODE -ne 0) { throw "Configure failed ($LASTEXITCODE)." }
            if ($Clean) {
                & $tools.CMake --build $buildDir --target clean
                if ($LASTEXITCODE -ne 0) { throw "Clean failed ($LASTEXITCODE)." }
            }
            [string[]]$buildTargets = if ($Test) {
                if ($TestTarget.Count -gt 0) { $TestTarget } else { 'all' }
            } else { $Target }
            # Keep this incremental: CMake/Ninja decides which changed outputs and dependent
            # consumers need work. Do not add --clean-first to the default app path.
            & $tools.CMake --build $buildDir --parallel 4 --target @buildTargets
            if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
            if ($tools.Clang) {
                $runtimeDir = Split-Path $tools.Clang -Parent
                $appDir = Join-Path $buildDir 'apps\desktop'
                if (Test-Path -LiteralPath $appDir) {
                    Get-ChildItem -LiteralPath $runtimeDir -Filter '*.dll' |
                        Copy-Item -Destination $appDir -Force
                }
            }
        }
        if (($Test -or $TestsOnly) -and -not $BuildOnly) {
            $cachePath = Join-Path $buildDir 'CMakeCache.txt'
            if (-not (Test-Path -LiteralPath $cachePath) -or
                -not (Select-String -LiteralPath $cachePath -Pattern '^PIKDRAW_BUILD_TESTS:BOOL=ON$' -Quiet)) {
                throw 'CTest targets are not configured. Restore test source code and configure with -Test before using -TestsOnly.'
            }
            $ctest = Join-Path (Split-Path $tools.CMake -Parent) 'ctest.exe'
            if (-not (Test-Path -LiteralPath $ctest)) { throw 'CTest was not found next to CMake.' }
            $testArgs = @('--test-dir', $buildDir, '--output-on-failure', '--no-tests=error')
            if ($TestRegex) { $testArgs += @('-R', $TestRegex) }
            & $ctest @testArgs
            if ($LASTEXITCODE -ne 0) { throw "CTest failed ($LASTEXITCODE)." }
        }
    } finally { Pop-Location }
} finally {
    if ($null -ne $lockStream) {
        $lockStream.Dispose()
        Remove-Item -LiteralPath $lockPath
    }
}
