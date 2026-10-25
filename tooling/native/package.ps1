param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = Join-Path $repoRoot 'build\win-dev'
$canonicalApp = [System.IO.Path]::GetFullPath(
    (Join-Path $buildDir 'apps\desktop\pikdraw.exe'))
$runningApp = @(Get-Process -Name pikdraw -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and [System.IO.Path]::GetFullPath($_.Path) -ieq $canonicalApp })
if ($runningApp.Count -gt 0) {
    $pids = ($runningApp | ForEach-Object Id) -join ', '
    throw "Cannot package while the canonical PikDraw executable is running (PID $pids). Close it after saving, then retry."
}

$builder = Join-Path $PSScriptRoot 'build.ps1'
& $builder -Configuration $Configuration -Target @('pikdraw-package')
if ($LASTEXITCODE -ne 0) { throw "Build/package target failed ($LASTEXITCODE)." }

$cpackConfig = Join-Path $buildDir 'CPackConfig.cmake'
if (-not (Test-Path -LiteralPath $cpackConfig -PathType Leaf)) {
    throw 'CMake did not generate CPackConfig.cmake.'
}
$configText = Get-Content -LiteralPath $cpackConfig -Raw
$nameMatch = [regex]::Match($configText, 'set\(CPACK_PACKAGE_FILE_NAME "([^"]+)"\)')
if (-not $nameMatch.Success) { throw 'CPackConfig.cmake has no package file name.' }
$packagePath = Join-Path (Join-Path $buildDir 'packages') ($nameMatch.Groups[1].Value + '.zip')
if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
    throw "CPack did not produce the expected ZIP: $packagePath"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($packagePath)
try {
    $entries = @{}
    foreach ($entry in $archive.Entries) {
        $entries[$entry.FullName.Replace('\', '/').TrimStart('/')] = $entry
    }
    $requiredEntries = @(
        'pikdraw.exe',
        'apps/mcp-server/pikdraw-mcp-server.exe',
        'apps/com-server/pikdraw-com-server.exe',
        'README.md',
        'LICENSE',
        'licenses/MIT-foundation.txt',
        'licenses/LICENSE.txt',
        'docs/AI-Automation-MCP.md',
        'docs/AI-Automation-COM.md',
        'docs/Windows-Package.md',
        'resources/icons/object-context-actions.svg',
        'resources/icons/tool-pen.svg',
        'resources/icons/tool-polygon.svg'
    )
    $missing = @($requiredEntries | Where-Object { -not $entries.ContainsKey($_) })
    if ($missing.Count -gt 0) {
        throw "Package is missing required entries: $($missing -join ', ')"
    }

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($binary in @(
            @{ Name = 'pikdraw.exe'; Path = Join-Path $buildDir 'apps\desktop\pikdraw.exe' },
            @{ Name = 'apps/mcp-server/pikdraw-mcp-server.exe'; Path = Join-Path $buildDir 'apps\mcp-server\pikdraw-mcp-server.exe' },
            @{ Name = 'apps/com-server/pikdraw-com-server.exe'; Path = Join-Path $buildDir 'apps\com-server\pikdraw-com-server.exe' }
        )) {
            $builtHash = (Get-FileHash -LiteralPath $binary.Path -Algorithm SHA256).Hash
            $stream = $entries[$binary.Name].Open()
            try {
                $archiveHash = [System.BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '')
            } finally {
                $stream.Dispose()
            }
            if ($archiveHash -ine $builtHash) {
                throw "Packaged binary '$($binary.Name)' differs from the canonical build output."
            }
        }
    } finally {
        $sha.Dispose()
    }
    Write-Output "Verified package: $packagePath"
    $packageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
    Write-Output "Entries: $($archive.Entries.Count); all three executables match the canonical build outputs."
    Write-Output "SHA256: $packageHash"
} finally {
    $archive.Dispose()
}
