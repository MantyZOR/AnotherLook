$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path -Parent $PSScriptRoot
function Fill-RoundRect($graphics, $brush, [single]$x, [single]$y, [single]$width, [single]$height, [single]$radius) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $d = $radius * 2
    $path.AddArc($x,$y,$d,$d,180,90)
    $path.AddArc(($x+$width-$d),$y,$d,$d,270,90)
    $path.AddArc(($x+$width-$d),($y+$height-$d),$d,$d,0,90)
    $path.AddArc($x,($y+$height-$d),$d,$d,90,90)
    $path.CloseFigure()
    $graphics.FillPath($brush,$path)
    $path.Dispose()
}
$frames = @()
foreach ($size in @(16,20,24,32,40,48,64,96,128,256)) {
    $large = [System.Drawing.Bitmap]::new(($size*4),($size*4),[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($large)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $factor = $size * 4.0 / 256
    $g.ScaleTransform($factor,$factor)
    $dark = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#14201D'))
    $light = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#B4CEAC'))
    $muted = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#6B8572'))
    Fill-RoundRect $g $dark 8 8 240 240 54
    Fill-RoundRect $g $light 52 58 66 140 14
    Fill-RoundRect $g $light 134 58 70 64 14
    Fill-RoundRect $g $muted 134 138 70 60 14
    $g.Dispose(); $dark.Dispose(); $light.Dispose(); $muted.Dispose()
    $small = [System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($small)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $g.DrawImage($large,0,0,$size,$size)
    $g.Dispose(); $large.Dispose()
    $png = [System.IO.MemoryStream]::new()
    $small.Save($png,[System.Drawing.Imaging.ImageFormat]::Png)
    $small.Dispose()
    $frames += @{Size=$size;Bytes=$png.ToArray()}
    $png.Dispose()
}
$file = [System.IO.File]::Create((Join-Path $projectRoot 'resources\app.ico'))
$writer = [System.IO.BinaryWriter]::new($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($frame in $frames) {
    $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
    $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$frame.Bytes.Length); $writer.Write([uint32]$offset)
    $offset += $frame.Bytes.Length
}
foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
$writer.Dispose()
