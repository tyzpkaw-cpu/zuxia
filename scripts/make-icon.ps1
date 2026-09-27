# Renders the character 足 into a multi-resolution .ico.
#
# Windows shows the text-service icon at several sizes -- the language bar
# strip is 16px while the input-method flyout is 32px or larger -- so every
# standard size is rendered separately rather than scaled from one bitmap.
# Small sizes drop the rounded plate and grow the glyph, because a plate plus
# padding leaves too few pixels for the character to stay legible at 16px.
param(
    [string]$Char = '足',
    [string]$Out  = 'app.ico',
    # Source Han Sans / 思源黑体 is OFL-1.1, so the rendered glyph can be
    # redistributed without the licence question a system font would raise.
    # Adobe's build registers as 思源黑体 (or Source Han Sans SC), Google's
    # rebuild of the same glyphs as Noto Sans SC.
    [string[]]$FontName = @('思源黑体', 'Source Han Sans SC', 'Noto Sans SC'),
    [string]$Weight = 'Bold'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# GDI+ silently substitutes a default face when a family is missing, which
# would quietly ship the wrong glyph. Resolve the family up front and fail
# loudly instead.
$installed = (New-Object System.Drawing.Text.InstalledFontCollection).Families
$family = $null
foreach ($candidate in $FontName) {
    $match = $installed | Where-Object { $_.Name -eq $candidate } | Select-Object -First 1
    if ($match) { $family = $match; break }
}
if (-not $family) {
    throw ("None of these font families is installed: {0}. Install Source Han Sans / 思源黑体 (OFL-1.1) or pass -FontName." -f ($FontName -join ', '))
}

$style = [System.Drawing.FontStyle]::$Weight
if (-not $family.IsStyleAvailable($style)) {
    $style = [System.Drawing.FontStyle]::Regular
    if (-not $family.IsStyleAvailable($style)) {
        throw ("Font family '{0}' offers neither {1} nor Regular." -f $family.Name, $Weight)
    }
}
Write-Output ("Rendering {0} with '{1}' ({2})" -f $Char, $family.Name, $style)

$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$streams = @()

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode     = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::Transparent)

    $plate = $size -ge 32
    if ($plate) {
        # Rounded square plate in the product ink colour.
        $inset  = [Math]::Max(1, [int]($size * 0.04))
        $radius = [int]($size * 0.22)
        $rect   = New-Object System.Drawing.Rectangle($inset, $inset,
                      ($size - 2 * $inset), ($size - 2 * $inset))
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $d = $radius * 2
        $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
        $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
        $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
        $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
        $path.CloseFigure()
        $brush = New-Object System.Drawing.SolidBrush(
            [System.Drawing.Color]::FromArgb(255, 27, 38, 59))
        $g.FillPath($brush, $path)
        $brush.Dispose(); $path.Dispose()
        $glyphColor = [System.Drawing.Color]::FromArgb(255, 245, 246, 248)
        $emScale = 0.62
    } else {
        # No plate: a dark glyph on transparency reads better at 16-24px.
        $glyphColor = [System.Drawing.Color]::FromArgb(255, 27, 38, 59)
        $emScale = 0.92
    }

    $font = New-Object System.Drawing.Font($family, ($size * $emScale),
        $style, [System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment     = 'Center'
    $format.LineAlignment = 'Center'
    $textBrush = New-Object System.Drawing.SolidBrush($glyphColor)
    $box = New-Object System.Drawing.RectangleF(0, 0, $size, $size)
    $g.DrawString($Char, $font, $textBrush, $box, $format)

    $textBrush.Dispose(); $format.Dispose(); $font.Dispose(); $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bitmap.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    $streams += , @{ Size = $size; Bytes = $ms.ToArray() }
    $ms.Dispose()
}

# Assemble the ICO container. Every entry is a PNG payload, which Windows has
# accepted since Vista and which keeps the 256px entry small.
$fs = [System.IO.File]::Create($Out)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)                    # reserved
$bw.Write([UInt16]1)                    # type: icon
$bw.Write([UInt16]$streams.Count)

$offset = 6 + 16 * $streams.Count
foreach ($entry in $streams) {
    $dim = if ($entry.Size -ge 256) { 0 } else { $entry.Size }
    $bw.Write([Byte]$dim)               # width
    $bw.Write([Byte]$dim)               # height
    $bw.Write([Byte]0)                  # palette colours
    $bw.Write([Byte]0)                  # reserved
    $bw.Write([UInt16]1)                # colour planes
    $bw.Write([UInt16]32)               # bits per pixel
    $bw.Write([UInt32]$entry.Bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $entry.Bytes.Length
}
foreach ($entry in $streams) { $bw.Write($entry.Bytes) }
$bw.Flush(); $bw.Close(); $fs.Close()

Write-Output ("{0}: {1} sizes, {2:N0} bytes" -f $Out, $streams.Count,
    (Get-Item $Out).Length)
