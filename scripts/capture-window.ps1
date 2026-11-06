# Capture one top-level window to a PNG (content only, even if partly covered).
# Usage: pwsh scripts/capture-window.ps1 -Title "RX 11" -Out docs/images/03-editor.png
param(
  [Parameter(Mandatory)][string]$Title,
  [Parameter(Mandatory)][string]$Out
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class Cap {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static IntPtr Find(string sub) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      if (!IsWindowVisible(h)) return true;
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (t.ToString().Contains(sub)) { found = h; return false; }
      return true; }, IntPtr.Zero);
    return found;
  }
  public static System.Drawing.Bitmap Grab(IntPtr h) {
    RECT r; GetWindowRect(h, out r);
    var bmp = new System.Drawing.Bitmap(r.R - r.L, r.B - r.T);
    using (var g = System.Drawing.Graphics.FromImage(bmp)) {
      IntPtr hdc = g.GetHdc();
      PrintWindow(h, hdc, 2); // PW_RENDERFULLCONTENT
      g.ReleaseHdc(hdc);
    }
    return bmp;
  }
}
"@ -ReferencedAssemblies System.Drawing
$h = [Cap]::Find($Title)
if ($h -eq [IntPtr]::Zero) { throw "No visible window with title containing '$Title'" }
$dir = Split-Path $Out -Parent
if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
$bmp = [Cap]::Grab($h)
$bmp.Save((Join-Path (Get-Location) $Out), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $Out"
