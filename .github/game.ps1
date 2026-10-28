Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Input {
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  public static void Press(IntPtr window, byte key) {
    SetForegroundWindow(window); System.Threading.Thread.Sleep(300);
    keybd_event(key, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(80); keybd_event(key, 0, 2, UIntPtr.Zero);
  }
  public static void Corner(IntPtr window) { SetWindowPos(window, IntPtr.Zero, 0, 0, 0, 0, 0x0001 | 0x0004 | 0x0010); }
  [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
  public static void Click(int x, int y) {
    SetCursorPos(x, y); System.Threading.Thread.Sleep(250);
    mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(80); mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
  }
  public static void ClickHolding(byte key, int x, int y) {
    keybd_event(key, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(80);
    Click(x, y);
    keybd_event(key, 0, 2, UIntPtr.Zero);
  }
  [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
  [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref Point p);
  public static Point Origin(IntPtr window) { var p = new Point(); ClientToScreen(window, ref p); return p; }
}
public static class Desktop {
  public delegate bool Visit(IntPtr h, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(Visit visit, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
  [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")] static extern IntPtr GetWindowLongPtrW(IntPtr h, int index);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder text, int cap);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder text, int cap);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
  public static IntPtr[] Of(int pid) {
    var found = new System.Collections.Generic.List<IntPtr>();
    EnumWindows((h, d) => { uint owner; GetWindowThreadProcessId(h, out owner); if (owner == (uint)pid) found.Add(h); return true; }, IntPtr.Zero);
    return found.ToArray();
  }
  public static string Describe(IntPtr h) {
    var cls = new System.Text.StringBuilder(64); GetClassNameW(h, cls, 64);
    var title = new System.Text.StringBuilder(128); GetWindowTextW(h, title, 128);
    Rect r; GetWindowRect(h, out r);
    return string.Format("{0} class={1} title={2} visible={3} iconic={4} rect={5},{6},{7},{8} ex=0x{9:x} owner={10}",
      h, cls, title, IsWindowVisible(h), IsIconic(h), r.L, r.T, r.R, r.B, GetWindowLongPtrW(h, -20).ToInt64(), GetWindow(h, 4));
  }
}
"@

function Notes([string]$line) { Add-Content (Join-Path $Out "notes.txt") $line }

function Capture([IntPtr]$handle, [string]$path) {
  $rect = New-Object Desktop+Rect
  [void][Desktop]::GetWindowRect($handle, [ref]$rect)
  $width = $rect.R - $rect.L; $height = $rect.B - $rect.T
  if ($width -le 0 -or $height -le 0 -or -not [Desktop]::IsWindowVisible($handle)) { return }
  $bitmap = New-Object System.Drawing.Bitmap $width, $height
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $dc = $graphics.GetHdc()
  [void][Desktop]::PrintWindow($handle, $dc, 2)
  $graphics.ReleaseHdc($dc)
  $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $graphics.Dispose()
  $bitmap.Dispose()
}

(New-Object -ComObject Shell.Application).MinimizeAll()
Start-Sleep -Seconds 2

try { Set-DisplayResolution -Width 1920 -Height 1080 -Force } catch { Write-Host "resolution stays: $_" }
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
Write-Host "screen $($screen.Width)x$($screen.Height)"

function Shot([string]$name) {
  $bitmap = New-Object System.Drawing.Bitmap $screen.Width, $screen.Height
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.CopyFromScreen($screen.Left, $screen.Top, 0, 0, $bitmap.Size)
  $bitmap.Save((Join-Path $Out "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $graphics.Dispose()
  $bitmap.Dispose()
}

function Rate([string]$file, [long]$from, [long]$to) {
  $frames = 0; $time = 0; $worst = 0
  foreach ($line in Get-Content $file) {
    $tick, $count, $elapsed, $slowest = $line.Split(" ")
    if ([long]$tick -le $from -or [long]$tick -gt $to) { continue }
    $frames += [int]$count
    $time += [long]$elapsed
    $worst = [Math]::Max($worst, [long]$slowest)
  }
  if ($time -eq 0) { return "-" }
  return "$([Math]::Round($frames * 1000.0 / $time, 1)) fps, slowest $worst ms"
}
