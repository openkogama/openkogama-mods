param([string]$Client, [string]$Server, [string]$Mods, [string]$Out, [string]$Template = "city", [int]$Seconds = 20)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Mods = (Resolve-Path $Mods).Path

. (Join-Path $PSScriptRoot "game.ps1")

$F2 = 0x71
$exe = Get-ChildItem $Client -Recurse -Filter *.exe | Where-Object { $_.Name -notmatch "crash|handler|UnityCrash" } | Select-Object -First 1
$dir = $exe.DirectoryName
$data = (New-Item -ItemType Directory -Force run).FullName
$env:OPENKOGAMA_TRACE = "1"
$hosting = Start-Process dotnet -ArgumentList "`"$(Resolve-Path (Join-Path $Server openkogama-server.dll))`"" -WorkingDirectory $data -PassThru -WindowStyle Hidden `
  -RedirectStandardOutput (Join-Path $Out "server.txt") -RedirectStandardError (Join-Path $Out "server-errors.txt")
Start-Sleep -Seconds 15
$world = (Invoke-RestMethod -Method Post "http://127.0.0.1:8080/api/worlds?name=Studio&template=$Template").id
Write-Host "world $world from $Template"

$config = Join-Path $env:LOCALAPPDATA "KogamaStudio\Config"
New-Item -ItemType Directory -Force $config | Out-Null
Set-Content (Join-Path $config "welcome_done.txt") "1"

$Tour = @(
  @{ name = "selected"; x = 80; y = 90 },
  @{ name = "clipboard"; x = 343; y = 373 },
  @{ name = "history"; x = 438; y = 373 },
  @{ name = "players"; x = 533; y = 373 },
  @{ name = "logic"; x = 348; y = 28 },
  @{ name = "settings"; x = 129; y = 9 },
  @{ name = "settings-closed"; x = 729; y = 419 },
  @{ name = "add-menu"; x = 112; y = 55 },
  @{ name = "circuit"; x = 160; y = 86 },
  @{ name = "reselect"; x = 80; y = 90 },
  @{ name = "add-menu-again"; x = 112; y = 55 },
  @{ name = "group-dialog"; x = 160; y = 69 },
  @{ name = "group-created"; x = 610; y = 349 },
  @{ name = "model"; x = 80; y = 107 },
  @{ name = "multi"; x = 80; y = 124; ctrl = $true },
  @{ name = "grouped"; x = 760; y = 92; wait = 4 },
  @{ name = "group-open"; x = 13; y = 345 }
)

$results = @()
function Session([string]$name, [string[]]$load, [switch]$Menu) {
  Remove-Item (Join-Path $dir "mods") -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force (Join-Path $dir "mods") | Out-Null
  foreach ($mod in $load) { Copy-Item (Join-Path $Mods "$mod.dll") (Join-Path $dir "mods") }
  $rates = Join-Path $dir "framerate.txt"
  Remove-Item $rates -ErrorAction SilentlyContinue

  $url = "http://127.0.0.1:8080/session?mode=edit&world=$world&key=$name&client=3.5.25&unity=6000.2.7f2"
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
  $row.closed = Rate $rates $from ([Environment]::TickCount64)
  Shot "$name-closed"

  if ($Menu) {
    [Input]::Press($window, $F2)
    Start-Sleep -Seconds 5
    Shot "$name-open"
    $index = 0
    foreach ($handle in [Desktop]::Of($game.Id)) {
      Notes "$name $([Desktop]::Describe($handle))"
      try { Capture $handle (Join-Path $Out "$name-window-$index.png") } catch { Notes "capture failed: $_" }
      $index++
    }
    $from = [Environment]::TickCount64
    Start-Sleep -Seconds $Seconds
    $row.open = Rate $rates $from ([Environment]::TickCount64)
    Shot "$name-open-later"
    $studio = [Desktop]::Of($game.Id) | Where-Object { [Desktop]::Describe($_) -match "class=OpenKogamaStudio" } | Select-Object -First 1
    if ($studio) {
      $rect = New-Object Desktop+Rect
      [void][Desktop]::GetWindowRect($studio, [ref]$rect)
      foreach ($step in $Tour) {
        if ($step.ctrl) { [Input]::ClickHolding(0x11, $rect.L + $step.x, $rect.T + $step.y) }
        else { [Input]::Click($rect.L + $step.x, $rect.T + $step.y) }
        Start-Sleep -Seconds $(if ($step.wait) { $step.wait } else { 2 })
        Shot "$name-$($step.name)"
      }
    }
  }

  Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 3
  Copy-Item $rates (Join-Path $Out "$name-framerate.txt")
  $log = Join-Path $env:LOCALAPPDATA "KogamaStudio\Logs\okmod-menu.log"
  if (Test-Path $log) { Copy-Item $log (Join-Path $Out "$name-studio-log.txt") }
  foreach ($crash in Get-ChildItem $env:TEMP -Recurse -Filter error.log -ErrorAction SilentlyContinue | Where-Object { $_.DirectoryName -match "Crash" }) {
    Copy-Item $crash.FullName (Join-Path $Out "$name-crash-$($crash.Directory.Name).txt")
  }
  $script:results += [pscustomobject]$row
}

try {
  Session "plain" @("framerate") -Menu
  Session "studio" @("framerate", "kogamastudio") -Menu
}
finally {
  Stop-Process -Id $hosting.Id -Force -ErrorAction SilentlyContinue
}

$table = $results | Format-Table -AutoSize | Out-String
Write-Host $table
if ($env:GITHUB_STEP_SUMMARY) {
  $lines = @("| session | menu closed fps | menu open fps |", "|---|---|---|")
  foreach ($row in $results) { $lines += "| $($row.session) | $($row.closed) | $(if ($row.open) { $row.open } else { '-' }) |" }
  Add-Content $env:GITHUB_STEP_SUMMARY ($lines -join "`n")
}
