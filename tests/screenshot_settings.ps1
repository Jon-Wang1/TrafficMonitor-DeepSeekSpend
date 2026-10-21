param(
    [string]$OutFile = "build\settings_dialog.png"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$process = Start-Process -FilePath (Join-Path $root 'build\plugin_test.exe') `
    -ArgumentList '--show-settings' -PassThru
Start-Sleep -Seconds 2

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    public static IntPtr FindByTitle(string title) { return FindWindow(null, title); }
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
"@

$hwnd = [Win32]::FindByTitle("DeepSeek 消费插件设置")

$bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$full = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$graphics = [System.Drawing.Graphics]::FromImage($full)
$graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $bounds.Size)
$graphics.Dispose()

$output = Join-Path $root $OutFile
if ($hwnd -ne [IntPtr]::Zero) {
    $rect = New-Object Win32+RECT
    [Win32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null

    $margin = 8
    $left = [Math]::Max(0, $rect.Left - $bounds.Left - $margin)
    $top = [Math]::Max(0, $rect.Top - $bounds.Top - $margin)
    $right = [Math]::Min($full.Width, $rect.Right - $bounds.Left + $margin)
    $bottom = [Math]::Min($full.Height, $rect.Bottom - $bounds.Top + $margin)
    $width = [Math]::Max(1, $right - $left)
    $height = [Math]::Max(1, $bottom - $top)

    $cropRect = New-Object System.Drawing.Rectangle $left, $top, $width, $height
    $crop = $full.Clone($cropRect, $full.PixelFormat)
    $crop.Save($output, [System.Drawing.Imaging.ImageFormat]::Png)
    $crop.Dispose()
} else {
    $full.Save($output, [System.Drawing.Imaging.ImageFormat]::Png)
}
$full.Dispose()

if ($hwnd -ne [IntPtr]::Zero) {
    [Win32]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
}
Start-Sleep -Seconds 1
if (-not $process.HasExited) {
    $process.Kill()
}

Write-Output ("screenshot saved: " + $OutFile + " (hwnd=" + $hwnd + ")")
