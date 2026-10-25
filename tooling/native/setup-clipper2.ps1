$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$version = '2.0.1'
$tag = 'Clipper2_2.0.1'
$archiveUrl = 'https://github.com/AngusJohnson/Clipper2/archive/refs/tags/Clipper2_2.0.1.zip'
$archiveSha256 = '2FBFE548AA1427716510B572446BCEC34C88990DF7DA510039CBDC2EE2406A23'
$toolchainRoot = Join-Path $repoRoot ".agent\toolchains\clipper2-$version"
$markerPath = Join-Path $toolchainRoot 'pikdraw-source-lock.json'
$expectedSource = Join-Path $toolchainRoot 'src\clipper.engine.cpp'
$expectedHeader = Join-Path $toolchainRoot 'include\clipper2\clipper.h'

if ((Test-Path -LiteralPath $markerPath -PathType Leaf) -and
    (Test-Path -LiteralPath $expectedSource -PathType Leaf) -and
    (Test-Path -LiteralPath $expectedHeader -PathType Leaf)) {
    $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
    if ($marker.version -eq $version -and $marker.archiveSha256 -eq $archiveSha256) {
        Write-Output "Clipper2 $version is already installed and matches the pinned archive."
        return
    }
    throw "The installed Clipper2 source does not match the pinned $version archive. Inspect '$toolchainRoot' before rebuilding."
}

if (Test-Path -LiteralPath $toolchainRoot) {
    throw "An incomplete Clipper2 directory already exists at '$toolchainRoot'. Inspect it before replacing it."
}

$tempBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$tempRoot = Join-Path $tempBase ("pikdraw-clipper2-$version-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$archivePath = Join-Path $tempRoot 'clipper2.zip'

try {
    Invoke-WebRequest -Uri $archiveUrl -OutFile $archivePath
    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualHash -ine $archiveSha256) {
        throw "Clipper2 archive SHA-256 mismatch: expected $archiveSha256, got $actualHash"
    }

    Expand-Archive -LiteralPath $archivePath -DestinationPath $tempRoot
    $sourceRoot = Join-Path $tempRoot "Clipper2-$tag\CPP\Clipper2Lib"
    $includeRoot = Join-Path $sourceRoot 'include'
    $sourceDirectory = Join-Path $sourceRoot 'src'
    foreach ($requiredPath in @(
        (Join-Path $includeRoot 'clipper2\clipper.h'),
        (Join-Path $includeRoot 'clipper2\clipper.engine.h'),
        (Join-Path $sourceDirectory 'clipper.engine.cpp')))
    {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Pinned Clipper2 archive is missing '$requiredPath'."
        }
    }

    $stagingRoot = Join-Path $tempRoot 'install'
    $stagingInclude = Join-Path $stagingRoot 'include'
    $stagingSource = Join-Path $stagingRoot 'src'
    New-Item -ItemType Directory -Path $stagingInclude,$stagingSource -Force | Out-Null
    Get-ChildItem -LiteralPath $includeRoot | Copy-Item -Destination $stagingInclude -Recurse
    Get-ChildItem -LiteralPath $sourceDirectory | Copy-Item -Destination $stagingSource -Recurse
    $lock = [ordered]@{
        name = 'Clipper2'
        version = $version
        tag = $tag
        archiveSha256 = $archiveSha256
        installedAtUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    }
    $lock | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stagingRoot 'pikdraw-source-lock.json') -Encoding utf8
    New-Item -ItemType Directory -Path (Split-Path $toolchainRoot -Parent) -Force | Out-Null
    Move-Item -LiteralPath $stagingRoot -Destination $toolchainRoot
    Write-Output "Installed pinned Clipper2 $version into '$toolchainRoot'."
} finally {
    $resolvedTempBase = [System.IO.Path]::GetFullPath($tempBase).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $resolvedTempRoot = [System.IO.Path]::GetFullPath($tempRoot)
    if ($resolvedTempRoot.StartsWith($resolvedTempBase, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
