param([string]$Path, [int]$W = 110)
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($Path)
$H = [int][math]::Round($bmp.Height * $W / $bmp.Width)
$stepX = [math]::Max(1, [int][math]::Ceiling($bmp.Width / $W))
$stepY = [math]::Max(1, [int][math]::Ceiling($bmp.Height / $H))
$out = ""
for ($y = 0; $y -lt $bmp.Height -and $out.Split("`n").Count -lt 60; $y += $stepY) {
    $line = ""
    for ($x = 0; $x -lt $bmp.Width; $x += $stepX) {
        $c = $bmp.GetPixel($x, $y)
        $lum = (0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B)
        $sat = [math]::Abs([int]$c.R - [int]$c.G) + [math]::Abs([int]$c.G - [int]$c.B) + [math]::Abs([int]$c.B - [int]$c.R)
        if ($sat -gt 80) { $line += '=' } elseif ($lum -gt 150) { $line += '.' } elseif ($lum -gt 60) { $line += '#' } else { $line += '@' }
    }
    $out += $line + "`n"
}
$bmp.Dispose()
$out