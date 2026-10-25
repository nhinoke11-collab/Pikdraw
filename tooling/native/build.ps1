# Public entrypoint. Queue and execution live in build_broker.py.
[CmdletBinding()]
param(
    [ValidateSet('Auto', 'Debug', 'Release')][string]$Configuration = 'Auto',
    [string[]]$Target = @('pikdraw'),
    [switch]$Clean, [switch]$Test, [switch]$TestsOnly,
    [string[]]$TestTarget = @(),
    [switch]$BuildOnly, [string]$TestRegex = '', [switch]$AllTests,
    [switch]$VerboseTests, [switch]$Reconfigure,
    [ValidateRange(0, 86400)][int]$LockWaitSeconds = 5,
    [ValidateRange(0, 256)][int]$Jobs = 0,
    [ValidateRange(1, 64)][int]$TestJobs = 1,
    [switch]$Submit, [switch]$Status, [switch]$Wait, [switch]$Cancel,
    [string]$RequestId = '', [string]$RetryFailed = '', [switch]$NoReuse,
    # Old callers receive an explicit rejection; these never bypass the lock.
    [switch]$NoLock, [switch]$Force
)
$ErrorActionPreference = 'Stop'
if ($NoLock -or $Force) { throw '-NoLock/-Force were removed. Use -Status/-Submit; never bypass the shared lock.' }
if ($TestTarget.Count -gt 0 -and -not $Test) { throw '-TestTarget requires -Test.' }
if ($TestRegex -and -not ($Test -or $TestsOnly)) { throw '-TestRegex requires -Test or -TestsOnly.' }
if ($BuildOnly -and (-not $Test -or $TestTarget.Count -eq 0)) { throw '-BuildOnly requires -Test and -TestTarget.' }
if ($TestsOnly -and ($Test -or $BuildOnly -or $TestTarget.Count -gt 0 -or $Clean -or $Reconfigure)) {
    throw '-TestsOnly cannot change configuration or build targets; use -Test for fresh binaries.'
}
if (($Test -or $TestsOnly) -and -not $BuildOnly -and -not $AllTests -and
    $TestTarget.Count -eq 0 -and -not $TestRegex) {
    throw 'Select -TestTarget/-TestRegex, or explicitly request -AllTests.'
}
if ($Test -and $TestTarget.Count -eq 0 -and -not $AllTests) {
    throw '-Test requires -TestTarget (or -AllTests); a regex alone cannot identify build dependencies.'
}
if ($AllTests -and ($TestRegex -or $TestTarget.Count -gt 0 -or -not ($Test -or $TestsOnly))) {
    throw '-AllTests requires -Test/-TestsOnly and cannot be combined with a narrower selection.'
}
if ($RequestId -and -not ($Status -or $Wait -or $Cancel)) { throw '-RequestId requires -Status/-Wait/-Cancel.' }
if ($Status -and ($Wait -or $Submit -or $RetryFailed -or $Cancel)) { throw '-Status is read-only; use it separately.' }
if ($Wait -and (-not $RequestId -or $Submit -or $RetryFailed -or $Cancel)) { throw '-Wait requires one -RequestId.' }
if ($Cancel -and (-not $RequestId -or $Submit -or $RetryFailed)) { throw '-Cancel requires one queued -RequestId.' }
if ($RetryFailed) {
    foreach ($name in @('Target','Test','TestTarget','TestRegex','AllTests','BuildOnly','TestsOnly','Clean','Reconfigure')) {
        if ($PSBoundParameters.ContainsKey($name)) { throw "-RetryFailed restores its own scope; do not combine with -$name." }
    }
}
if ($Submit -and $TestsOnly) { throw 'Use -Submit -Test with explicit targets so queued tests build fresh inputs.' }
. (Join-Path $PSScriptRoot 'env.ps1')
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Assert-PikDrawPrimarySource $repoRoot
$tools = Get-PikDrawToolchain -PythonOnly:($Status -or $Wait -or $Cancel)
if (-not $tools.Python) { throw 'The build coordinator requires Python 3.10+ (PIKDRAW_PYTHON is supported).' }
$tools.PowerShell = (Get-Process -Id $PID).Path
$request = @{
    configuration = $Configuration; targets = @($Target); clean = [bool]$Clean
    test = [bool]$Test; tests_only = [bool]$TestsOnly; test_targets = @($TestTarget)
    build_only = [bool]$BuildOnly; test_regex = $TestRegex; all_tests = [bool]$AllTests
    verbose_tests = [bool]$VerboseTests; reconfigure = [bool]$Reconfigure
    jobs = $Jobs; test_jobs = $TestJobs; no_reuse = [bool]$NoReuse
}
$retryOverrides = @{}
foreach ($pair in @(@('Configuration','configuration'), @('Jobs','jobs'), @('TestJobs','test_jobs'), @('VerboseTests','verbose_tests'))) {
    if ($PSBoundParameters.ContainsKey($pair[0])) { $retryOverrides[$pair[1]] = $request[$pair[1]] }
}
$payload = @{
    root = $repoRoot; tools = $tools; request = $request
    submit = [bool]$Submit; status = [bool]$Status; wait = [bool]$Wait; cancel = [bool]$Cancel
    request_id = $RequestId; retry_failed = $RetryFailed; lock_wait_seconds = $LockWaitSeconds
    retry_overrides = $retryOverrides
} | ConvertTo-Json -Depth 8 -Compress
$encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload))
& $tools.Python -B (Join-Path $PSScriptRoot 'build_broker.py') --payload $encoded
$brokerExit = $LASTEXITCODE
if ($brokerExit -eq 75) {
    throw 'Build request is still queued (not failed). Use the printed request ID with -Status/-Wait; do not resubmit.'
}
if ($brokerExit -ne 0) { throw "Build coordinator failed ($brokerExit). See the request result above." }
