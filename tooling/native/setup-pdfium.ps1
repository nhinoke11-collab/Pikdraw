Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$toolchainRoot = Join-Path $repoRoot '.agent\toolchains\pdfium-8100'
$archivePath = Join-Path $toolchainRoot 'pdfium-win-x64.tgz'
$runtimePath = Join-Path $toolchainRoot 'bin\pdfium.dll'
$versionPath = Join-Path $toolchainRoot 'VERSION'
$expectedHash = '51621bcc1d9be9ea448f4b84b0516862b928e3968d3fca899af1a6d6e2848d6d'
$expectedRuntimeHash = 'ae913f407e8d3f5cdbf08a9869564d647b13e132e01eb376c0a5a33b7a227a45'
$releaseUrl = 'https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8100/pdfium-win-x64.tgz'

$resolvedToolchainRoot = [System.IO.Path]::GetFullPath($toolchainRoot)
if (-not $resolvedToolchainRoot.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'PDFium destination escaped the repository toolchain directory.'
}
New-Item -ItemType Directory -Force -Path $toolchainRoot | Out-Null

$isInstalled = (Test-Path -LiteralPath $runtimePath) -and (Test-Path -LiteralPath $versionPath)
if ($isInstalled) {
    $versionText = Get-Content -LiteralPath $versionPath -Raw
    $isInstalled = $versionText -match '(?m)^MAJOR=157\s*$' -and
        $versionText -match '(?m)^MINOR=0\s*$' -and
        $versionText -match '(?m)^BUILD=8100\s*$' -and
        $versionText -match '(?m)^PATCH=0\s*$'
}
if ($isInstalled) {
    $runtimeHash = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($runtimeHash -ne $expectedRuntimeHash) {
        throw "Installed PDFium runtime DLL SHA-256 mismatch: $runtimeHash"
    }
    if (Test-Path -LiteralPath $archivePath) {
        $cachedArchiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($cachedArchiveHash -ne $expectedHash) {
            throw "Cached PDFium archive SHA-256 mismatch: $cachedArchiveHash"
        }
    }
    Write-Output 'PDFium 157.0.8100.0 is already installed in .agent/toolchains/pdfium-8100.'
    exit 0
}

Invoke-WebRequest -Uri $releaseUrl -OutFile $archivePath
$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedHash) {
    throw "Pinned PDFium archive SHA-256 mismatch: $actualHash"
}

$entries = & tar.exe -tzf $archivePath
if ($LASTEXITCODE -ne 0 -or $entries.Count -eq 0) {
    throw 'Pinned PDFium archive could not be listed.'
}
foreach ($entry in $entries) {
    $normalized = $entry.Replace('\', '/')
    if ($normalized.StartsWith('/') -or $normalized -match '(^|/)\.\.(?:/|$)' -or
        $normalized -match '^[A-Za-z]:') {
        throw "Unsafe archive entry in pinned PDFium package: $entry"
    }
}
& tar.exe -xzf $archivePath -C $toolchainRoot
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $runtimePath) -or
    -not (Test-Path -LiteralPath $versionPath)) {
    throw 'Pinned PDFium package extraction did not produce its runtime and version marker.'
}
$runtimeHash = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($runtimeHash -ne $expectedRuntimeHash) {
    throw "Extracted PDFium runtime DLL SHA-256 mismatch: $runtimeHash"
}
$versionText = Get-Content -LiteralPath $versionPath -Raw
if ($versionText -notmatch '(?m)^MAJOR=157\s*$' -or
    $versionText -notmatch '(?m)^BUILD=8100\s*$') {
    throw 'Extracted PDFium version marker did not match the pinned release.'
}
Write-Output 'Installed PDFium 157.0.8100.0 after validating the pinned archive hash.'
