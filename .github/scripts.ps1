param([string]$Client, [string]$Server, [string]$Mods, [string]$Scripts, [string]$Plugins, [string]$Out, [string]$Version = "3.5.25", [string]$Unity = "6000.2.7f2",
  [string]$Template = "city", [int]$Seconds = 20)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Mods = (Resolve-Path $Mods).Path
$Scripts = (Resolve-Path $Scripts).Path

. (Join-Path $PSScriptRoot "game.ps1")

$F7 = 0x76
$exe = Get-ChildItem $Client -Recurse -Filter *.exe | Where-Object { $_.Name -notmatch "crash|handler|UnityCrash" } | Select-Object -First 1
$dir = $exe.DirectoryName
$data = (New-Item -ItemType Directory -Force run).FullName

function Logo([string]$path) {
  $bitmap = New-Object System.Drawing.Bitmap 96, 96
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $graphics.Clear([System.Drawing.Color]::Transparent)
  $graphics.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 47, 140, 232))), 4, 4, 88, 88)
  $font = New-Object System.Drawing.Font "Segoe UI", 26, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
  $format = New-Object System.Drawing.StringFormat
  $format.Alignment = [System.Drawing.StringAlignment]::Center
  $format.LineAlignment = [System.Drawing.StringAlignment]::Center
  $graphics.DrawString("Lua", $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF 0, 0, 96, 96), $format)
  $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $graphics.Dispose()
  $bitmap.Dispose()
}

Copy-Item $Scripts (Join-Path $data "scripts") -Recurse
Logo (Join-Path $data "scripts\hud\logo.png")
New-Item -ItemType Directory -Force (Join-Path $data "plugins") | Out-Null
Copy-Item (Join-Path $Plugins "online.dll") (Join-Path $data "plugins")
$hosting = Start-Process dotnet -ArgumentList "`"$(Resolve-Path (Join-Path $Server openkogama-server.dll))`"" -WorkingDirectory $data -PassThru -WindowStyle Hidden `
  -RedirectStandardOutput (Join-Path $Out "server.txt") -RedirectStandardError (Join-Path $Out "server-errors.txt")
Start-Sleep -Seconds 15
$world = (Invoke-RestMethod -Method Post "http://127.0.0.1:8080/api/worlds?name=Scripts&template=$Template").id
Write-Host "world $world from $Template"


$results = @()
function Session([string]$name, [string[]]$load, [switch]$Hud) {
  Remove-Item (Join-Path $dir "mods") -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force (Join-Path $dir "mods") | Out-Null
  foreach ($mod in $load) { Copy-Item (Join-Path $Mods "$mod.dll") (Join-Path $dir "mods") }
  $rates = Join-Path $dir "framerate.txt"
  Remove-Item $rates -ErrorAction SilentlyContinue

  $url = "http://127.0.0.1:8080/session?mode=play&world=$world&key=$name&client=$Version&unity=$Unity"
  $package = "kogamaPackage:" + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($url))
  $game = Start-Process $exe.FullName -WorkingDirectory $dir -PassThru -ArgumentList @($package, "-logFile", (Join-Path $Out "$name-client.txt"),
    "-screen-fullscreen", "0", "-screen-width", "960", "-screen-height", "540")

  $deadline = (Get-Date).AddSeconds(240)
  while ((Get-Date) -lt $deadline -and -not ((Test-Path $rates) -and (Get-Content $rates).Count -ge 5)) { Start-Sleep -Seconds 2 }
  if (-not (Test-Path $rates)) {
    Shot "$name-stuck"
    Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue
    throw "$name never reached a world"
  }
  $game.Refresh()
  $window = $game.MainWindowHandle
  [Input]::Corner($window)
  Start-Sleep -Seconds 20

  $row = [ordered]@{ session = $name }
  $from = [Environment]::TickCount64
  Start-Sleep -Seconds $Seconds
  $row.fps = Rate $rates $from ([Environment]::TickCount64)
  Shot "$name-world"

  if ($Hud) {
    $log = Join-Path $dir "scripts.log"
    $ready = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $ready -and -not ((Test-Path $log) -and (Select-String -Path $log -Pattern "\[server:hud\] started" -Quiet))) { Start-Sleep -Seconds 2 }
    $origin = [Input]::Origin($window)
    Notes "$name client origin $($origin.X),$($origin.Y)"
    foreach ($i in 1..2) {
      [Input]::Click($origin.X + 124, $origin.Y + 163)
      Start-Sleep -Seconds 1
    }
    Start-Sleep -Seconds 2
    Shot "$name-clicked"
    [Input]::Press($window, $F7)
    Add-Content (Join-Path $data "scripts\hud\main.lua") 'print("server update arrived")'
    Start-Sleep -Seconds 15
    Shot "$name-updated"
  }

  Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 3
  Copy-Item $rates (Join-Path $Out "$name-framerate.txt")
  $log = Join-Path $dir "scripts.log"
  if (Test-Path $log) { Copy-Item $log (Join-Path $Out "$name-log.txt") }
  foreach ($crash in Get-ChildItem $env:TEMP -Recurse -Filter error.log -ErrorAction SilentlyContinue | Where-Object { $_.DirectoryName -match "Crash" }) {
    Copy-Item $crash.FullName (Join-Path $Out "$name-crash-$($crash.Directory.Name).txt")
  }
  $script:results += [pscustomobject]$row
}

try {
  Session "plain" @("framerate")
  Session "scripts" @("framerate", "scripts") -Hud
}
finally {
  Stop-Process -Id $hosting.Id -Force -ErrorAction SilentlyContinue
}

$log = Join-Path $Out "scripts-log.txt"
$text = if (Test-Path $log) { Get-Content $log -Raw } else { "" }
$checks = [ordered]@{
  "the server offered the hud script" = "scripts from http://127\.0\.0\.1:8080: hud"
  "the hud script started" = "\[server:hud\] started"
  "scripts reached the game's drawing" = "scripts draw into the game"
  "the button took two clicks" = "\[server:hud\] clicked 2"
  "keys reached the script" = "\[server:hud\] pressed F7"
  "a change on the server reached the game" = "\[server:hud\] server update arrived"
  "a plugin's script reached the game" = "\[server:online\] started"
  "the server sent the script a message" = "\[server:online\] online: Player"
  "the script asked the server and got an answer" = "\[server:online\] pong after"
}
$missing = @()
$worlds = [regex]::Matches($text, "\[server:hud\] world loaded").Count
$checks["the world event came once for each start"] = if ($worlds -ge 1 -and $worlds -le [regex]::Matches($text, "\[server:hud\] started").Count) { "." } else { "(?!)" }
$lines = @("| session | fps |", "|---|---|")
foreach ($row in $results) { $lines += "| $($row.session) | $($row.fps) |" }
$lines += ""
foreach ($check in $checks.GetEnumerator()) {
  $passed = $text -match $check.Value
  $lines += "- $(if ($passed) { 'ok' } else { 'missing' }): $($check.Key)"
  if (-not $passed) { $missing += $check.Key }
}
Write-Host ($lines -join "`n")
if ($env:GITHUB_STEP_SUMMARY) { Add-Content $env:GITHUB_STEP_SUMMARY ($lines -join "`n") }
if ($missing.Count) { throw "missing: $($missing -join ', ')" }
