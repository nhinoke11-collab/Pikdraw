# Array splatting does not bind strings like '-Target' as parameter names.
# Turn wrapper arguments into a hashtable using the destination script's metadata.
function Invoke-PikDrawNative {
    param([string]$Script, [object[]]$ForwardArgs, [hashtable]$Defaults = @{})
    $metadata = (Get-Command -Name $Script -ErrorAction Stop).Parameters
    $bound = @{}
    foreach ($key in $Defaults.Keys) { $bound[$key] = $Defaults[$key] }
    for ($index = 0; $index -lt $ForwardArgs.Count; $index++) {
        $token = [string]$ForwardArgs[$index]
        if (-not $token.StartsWith('-')) { throw "Expected a named option, received '$token'." }
        $key = $token.TrimStart('-')
        if (-not $metadata.ContainsKey($key)) { throw "Unknown option: $token. See docs/development/native-build.md." }
        if ($metadata[$key].ParameterType -eq [Management.Automation.SwitchParameter]) {
            $bound[$key] = $true
            continue
        }
        $values = @()
        while ($index + 1 -lt $ForwardArgs.Count) {
            $next = $ForwardArgs[$index + 1]
            if ($next -is [string] -and $next.StartsWith('-')) { break }
            $index++
            $values += @($next)
        }
        if ($values.Count -eq 0) { throw "$token requires a value." }
        if ($metadata[$key].ParameterType.IsArray) {
            $bound[$key] = @($values | ForEach-Object { ([string]$_).Split(',') })
        } elseif ($values.Count -eq 1) {
            $bound[$key] = $values[0]
        } else {
            throw "$token accepts one value. Use -TestTarget/-Target for arrays."
        }
    }
    & $Script @bound
}
