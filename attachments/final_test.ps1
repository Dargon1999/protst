$ErrorActionPreference = 'Continue'
$out = 'E:\Projeck\RamBoostPro\real\final'
$m = 'E:\Projeck\RamBoostPro\real\final_marks.txt'
New-Item -ItemType Directory -Path $out -Force | Out-Null
Get-ChildItem $out -Filter '*.png' | Remove-Item
Remove-Item $m -ErrorAction SilentlyContinue
function L($s) { ("[{0}] {1}" -f (Get-Date -Format HH:mm:ss), $s) | Out-File $m -Append }
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Fz {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr z, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
public struct RECT { public int Left,Top,Right,Bottom; }
public delegate bool EnumProc(IntPtr h, IntPtr lp);
'@
[Fz]::SetProcessDPIAware() | Out-Null
function FindMain {
  $script:fm = [IntPtr]::Zero
  [Fz]::EnumWindows({ param($h,$l)
    $sb = New-Object System.Text.StringBuilder 256
    [Fz]::GetClassNameW($h,$sb,256) | Out-Null
    if ($sb.ToString() -eq 'RamBoostProMain') {
      $r = New-Object RECT
      [Fz]::GetWindowRect($h,[ref]$r) | Out-Null
      if ([Fz]::IsWindowVisible($h) -and (($r.Right) -gt ($r.Left + 400))) { $script:fm = $h }
    }
    return ([IntPtr]$script:fm -eq [IntPtr]::Zero)
  },[IntPtr]::Zero) | Out-Null
  return $script:fm
}
function CenterScreen($h,$cx,$cy,$phyW,$phyH,$scale) {
  $r = New-Object RECT
  [Fz]::GetWindowRect($h,[ref]$r) | Out-Null
  $scrW = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea.Width
  $scrH = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea.Height
  $x = [int](($scrW - $phyW) / 2)
  $y = [int](($scrH - $phyH) / 2)
  return "$x $y"
}
function RealClick($h,$cx,$cy,$scale) {
  $r = New-Object RECT
  [Fz]::GetWindowRect($h,[ref]$r) | Out-Null
  $sx = $r.Left + [int]($cx * $scale)
  $sy = $r.Top + [int]($cy * $scale)
  [Fz]::SetCursorPos($sx,$sy) | Out-Null
  Start-Sleep -Milliseconds 120
  [Fz]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 150
  [Fz]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
}
function Resize($h,$cx,$cy) {
  $r = New-Object RECT
  $zz = [IntPtr]::Zero
  [Fz]::SetWindowPos($h,$zz,120,90,$cx,$cy,0x0040) | Out-Null
  Start-Sleep -Milliseconds 900
}
function Capture($h,$path) {
  $r = New-Object RECT
  [Fz]::GetWindowRect($h,[ref]$r) | Out-Null
  $b = New-Object System.Drawing.Bitmap(($r.Right-$r.Left),($r.Bottom-$r.Top))
  $g = [System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($r.Left,$r.Top,0,0,$b.Size)
  $g.Dispose()
  $b.Save($path,[System.Drawing.Imaging.ImageFormat]::Png)
  $b.Dispose()
}
Add-Type -AssemblyName System.Windows.Forms
$temp = [System.IO.Path]::GetTempPath()
try {
  Stop-Process -Name RamBoostPro -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 700
  $p = Start-Process -FilePath 'E:\Projeck\RamBoostPro\build\RamBoostPro.exe' -WorkingDirectory 'E:\Projeck\RamBoostPro'
  Start-Sleep -Seconds 4
  $h = FindMain
  if ($h -eq [IntPtr]::Zero) { L 'NO MAIN'; exit }
  $zeta = 1.25
  L ("hwnd=0x{0:X}" -f $h.ToInt64())
  RealClick $h 310 63 $zeta
  Start-Sleep -Milliseconds 900
  Capture $h "$out\f0_temp_empty.png"
  RealClick $h 100 158 $zeta
  L 'scan clicked'
  Start-Sleep -Seconds 140
  L ("resp=" + $p.Responding)
  Capture $h "$out\f1_scan_done.png"
  RealClick $h 653 158 $zeta
  Start-Sleep -Milliseconds 700
  Capture $h "$out\f2_selnone.png"
  RealClick $h 43 295 $zeta
  Start-Sleep -Milliseconds 700
  Capture $h "$out\f3_row_checked.png"
  RealClick $h 311 158 $zeta
  L 'delete clicked'
  Start-Sleep -Seconds 30
  Capture $h "$out\f4_delete_done.png"
  Resize $h 1000 688
  Capture $h "$out\f5_1000x688.png"
  Resize $h 1250 875
  Capture $h "$out\f6_1250x875.png"
  Resize $h 980 621
  Capture $h "$out\f7_980x621.png"
  $fc = @(Get-ChildItem $temp -Recurse -File -ErrorAction SilentlyContinue).Count
  L ("files-after=" + $fc)
  foreach ($f in @(Get-ChildItem $out -Filter '*.png')) { L ("  " + $f.Name + " " + (Get-FileHash $f.FullName -Algorithm MD5).Hash.Substring(0,12)) }
  if (Test-Path "C:\Users\Sanya\AppData\Local\Temp\rbp_dbg.txt") { L '--- rbp_dbg:'; Get-Content "C:\Users\Sanya\AppData\Local\Temp\rbp_dbg.txt" | ForEach-Object { L ("    " + $_) } }
  Stop-Process -Name RamBoostPro -Force -ErrorAction SilentlyContinue
} catch { L ("FAIL " + $_.Exception.Message); Stop-Process -Name RamBoostPro -Force -ErrorAction SilentlyContinue }
"done" | Out-File $m -Append