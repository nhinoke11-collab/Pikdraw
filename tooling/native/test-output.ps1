# Shared path checks for generated output. Never follow a junction or symlink.
function Assert-PikDrawOutputPath([string]$Path, [string]$WorkspaceRoot) {
    $root = [IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\', '/')
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($root + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output is outside the workspace: $full"
    }
    $cursor = $full
    while ($cursor.Length -ge $root.Length) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing output through a reparse point: $cursor"
            }
        }
        if ($cursor.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { break }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}

function Remove-PikDrawTestRun([string]$RunDirectory, [string]$WorkspaceRoot) {
    $full = Assert-PikDrawOutputPath $RunDirectory $WorkspaceRoot
    $parent = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot 'build\win-dev\test-output'))
    if (-not ([IO.Path]::GetDirectoryName($full)).Equals($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($full) -notmatch '^\d+-[a-f0-9]{32}$' -or
        -not (Test-Path -LiteralPath (Join-Path $full '.pikdraw-test-run') -PathType Leaf)) {
        throw "Not an owned CTest output directory: $full"
    }
    # Validate the complete subtree before the first deletion.
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($full)
    while ($pending.Count -gt 0) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            $null = Assert-PikDrawOutputPath $item.FullName $WorkspaceRoot
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
    Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction Stop
}
