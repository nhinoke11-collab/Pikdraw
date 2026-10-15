Add-Type -AssemblyName System.Drawing
$names = @('m1_pikdraw_light_mode', 'm1_pikdraw_settings_dark', 'm1_pikdraw_settings_light', 'm1_pikdraw_dark_mode', 'm1_pikdraw_settings_units', 'm1_pikdraw_settings_cnc')
foreach ($n in $names) {
    $bmpPath = Resolve-Path ("docs/architecture/ui-reference/" + $n + ".bmp")
    $pngPath = [System.IO.Path]::ChangeExtension($bmpPath, ".png")
    $img = [System.Drawing.Image]::FromFile($bmpPath)
    $img.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $img.Dispose()
    Write-Host "Converted: $pngPath"
}
