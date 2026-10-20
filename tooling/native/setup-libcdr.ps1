Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'env.ps1')
$tools = Get-PikDrawToolchain
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$toolchainRoot = Join-Path $repoRoot '.agent\toolchains\libcdr-msys2'
$cacheRoot = Join-Path $toolchainRoot 'packages'
$extractRoot = Join-Path $toolchainRoot 'root'
$pythonDeps = Join-Path $toolchainRoot 'python-deps'
$resolvedToolchainRoot = [System.IO.Path]::GetFullPath($toolchainRoot)
if (-not $resolvedToolchainRoot.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'libcdr destination escaped the repository toolchain directory.'
}
New-Item -ItemType Directory -Force -Path $cacheRoot, $extractRoot | Out-Null

$packages = @(
    @{ Name = 'mingw-w64-ucrt-x86_64-gcc-16.2.0-6-any.pkg.tar.zst'; Sha256 = '0290e91f5a9a8a3699bcea6221a899403bb9bd5d6f7dbd98c883d74e2ef36338' },
    @{ Name = 'mingw-w64-ucrt-x86_64-cc-libs-16.2.0-6-any.pkg.tar.zst'; Sha256 = 'd8fff9e62443168bcda8ce746697243519c4f090ea607357ce94feac0bd10a50' },
    @{ Name = 'mingw-w64-ucrt-x86_64-libgcc-16.2.0-6-any.pkg.tar.zst'; Sha256 = '80b2497f019977dd9528b1abc8621b1328a0115ab1f724047ccbb56bf323f11d' },
    @{ Name = 'mingw-w64-ucrt-x86_64-libstdc++-16.2.0-6-any.pkg.tar.zst'; Sha256 = '33c5e58a85a538cf843a8672991cce591f0231d66da3b66a57629712d64be17f' },
    @{ Name = 'mingw-w64-ucrt-x86_64-libwinpthread-14.0.0.r495.g63ebe6cdb-1-any.pkg.tar.zst'; Sha256 = '1f847f4f5d56d8f54cbc9b5ce339c36f4f208353e813d4b1e5648cf33138b9a2' },
    @{ Name = 'mingw-w64-ucrt-x86_64-icu-78.3-4-any.pkg.tar.zst'; Sha256 = 'db9282bbd0a2e74c31d66af6bd89eb68d647a09f721eef4c661768588dca4324' },
    @{ Name = 'mingw-w64-ucrt-x86_64-lcms2-2.19.1-1-any.pkg.tar.zst'; Sha256 = '144518cc80679f7bcc1d31b5dd7ec92e8737a1d8162014e30c8c1c181f47a156' },
    @{ Name = 'mingw-w64-ucrt-x86_64-libcdr-0.1.9-1-any.pkg.tar.zst'; Sha256 = '5a1616e3411914b50a04828449b42e1039e04ded8345aa1abd272e417b6f7e38' },
    @{ Name = 'mingw-w64-ucrt-x86_64-librevenge-0.0.6-1-any.pkg.tar.zst'; Sha256 = 'c614473fdacab65f941ed23ea29c5f13582dfc2355f07d3cf6079258589b192f' },
    @{ Name = 'mingw-w64-ucrt-x86_64-zlib-ng-compat-2.3.3-2-any.pkg.tar.zst'; Sha256 = 'b3ca6b437fb154a0fa26652002cdda591498ee4a0e08d91cc653c3702d090f25' }
)

foreach ($package in $packages) {
    $archive = Join-Path $cacheRoot $package.Name
    if (-not (Test-Path -LiteralPath $archive)) {
        $url = 'https://mirror.msys2.org/mingw/ucrt64/' + $package.Name
        Invoke-WebRequest -Uri $url -OutFile $archive
    }
    $actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $package.Sha256) {
        throw "Pinned MSYS2 package SHA-256 mismatch for $($package.Name): $actualHash"
    }
}

$pythonPaths = @($pythonDeps, (Join-Path $toolchainRoot 'py-zstandard'))
if ($env:PYTHONPATH) { $pythonPaths += $env:PYTHONPATH }
$env:PYTHONPATH = $pythonPaths -join ';'
$pythonProbe = & $tools.Python -c 'import zstandard' 2>$null
if ($LASTEXITCODE -ne 0) {
    New-Item -ItemType Directory -Force -Path $pythonDeps | Out-Null
    & $tools.Python -m pip install --disable-pip-version-check --only-binary=:all: --target $pythonDeps zstandard==0.25.0
    if ($LASTEXITCODE -ne 0) { throw 'Could not install the pinned zstandard extractor dependency.' }
}
$pythonPaths = @($pythonDeps, (Join-Path $toolchainRoot 'py-zstandard'))
if ($env:PYTHONPATH) { $pythonPaths += $env:PYTHONPATH }
$env:PYTHONPATH = $pythonPaths -join ';'
$extractor = Join-Path $toolchainRoot 'extract-msys2-package.py'
@'
import pathlib, shutil, sys, tarfile, zstandard

archive, destination = sys.argv[1:]
root = pathlib.Path(destination).resolve()
with open(archive, "rb") as source:
    reader = zstandard.ZstdDecompressor().stream_reader(source)
    with tarfile.open(fileobj=reader, mode="r|") as package:
        for entry in package:
            relative = pathlib.PurePosixPath(entry.name.replace("\\", "/"))
            if relative.is_absolute() or any(part in ("", ".", "..") for part in relative.parts):
                raise SystemExit(f"unsafe package entry: {entry.name}")
            target = root.joinpath(*relative.parts).resolve()
            if root != target and root not in target.parents:
                raise SystemExit(f"package entry escaped destination: {entry.name}")
            if entry.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif entry.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                content = package.extractfile(entry)
                if content is None:
                    raise SystemExit(f"could not read package entry: {entry.name}")
                with content, open(target, "wb") as output:
                    shutil.copyfileobj(content, output)
            elif entry.issym() or entry.islnk():
                continue
            else:
                raise SystemExit(f"unsupported package entry type: {entry.name}")
'@ | Set-Content -LiteralPath $extractor -Encoding UTF8
try {
    foreach ($package in $packages) {
        $archive = Join-Path $cacheRoot $package.Name
        & $tools.Python $extractor $archive $extractRoot
        if ($LASTEXITCODE -ne 0) { throw "Could not extract pinned package $($package.Name)." }
    }
} finally {
    Remove-Item -LiteralPath $extractor -Force -ErrorAction SilentlyContinue
}

$ucrt = Join-Path $extractRoot 'ucrt64'
$required = @(
    (Join-Path $ucrt 'include\libcdr-0.1\libcdr\libcdr.h'),
    (Join-Path $ucrt 'include\librevenge-0.0\librevenge\librevenge.h'),
    (Join-Path $ucrt 'bin\libcdr-0.1.dll'),
    (Join-Path $ucrt 'bin\librevenge-0.0.dll'),
    (Join-Path $ucrt 'bin\libwinpthread-1.dll')
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Pinned libcdr setup is incomplete: $path" }
}
Write-Output 'Installed pinned libcdr 0.1.9 / librevenge 0.0.6 UCRT64 packages in .agent/toolchains/libcdr-msys2/root.'
