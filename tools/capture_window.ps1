# 抓取指定进程的主窗口（含标题栏/边框），用于验证客户端外观
# 用法: powershell -File tools/capture_window.ps1 -ProcessName EPH205 -Out shot.png
param([string]$ProcessName = "EPH205", [string]$Out = "window.png")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Capture {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
"@

$p = Get-Process $ProcessName -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Error "找不到 $ProcessName 的主窗口"; exit 1 }

$h = $p.MainWindowHandle
[Win32Capture]::ShowWindow($h, 9) | Out-Null    # SW_RESTORE
[Win32Capture]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700

$rect = New-Object Win32Capture+RECT
[Win32Capture]::GetWindowRect($h, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$hgt = $rect.Bottom - $rect.Top
if ($w -le 0 -or $hgt -le 0) { Write-Error "窗口尺寸异常"; exit 1 }

Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[Win32Capture]::PrintWindow($h, $hdc, 2) | Out-Null   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "$Out ${w}x${hgt}"
