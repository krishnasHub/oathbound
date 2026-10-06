<#
  Takes the README gallery: a set of game moments captured in parallel, saved as JPEGs in docs/screenshots/.

    .\tools\screenshots.ps1            # all shots
    .\tools\screenshots.ps1 -Only dusk,night

  Each shot is its own game process (-RPGShot = seconds in, then it quits). JPEGs, not PNGs: PNGs go through
  Git LFS (1 GB free on GitHub), a JPEG is ~200 KB in plain git.
#>
param([string[]] $Only = @(), [int] $Parallel = 6)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Proj = Join-Path $Root "unreal\ActionRPG.uproject"
$Editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$ShotDir = Join-Path $Root "unreal\Saved\Screenshots\RPG"
$LogDir = Join-Path $Root "unreal\Saved\Logs\Shots"
$OutDir = Join-Path $Root "docs\screenshots"
New-Item -ItemType Directory -Force $OutDir, $LogDir | Out-Null

# name = (seconds in, extra arguments[, the PNG to keep, when a test scenario takes its own shot])
$shots = [ordered]@{
    "01-title"        = @(1.5,  "")
    "02-select-mage"  = @(10.6, "-RPGAutoSelect=mage")
    "03-select-thief" = @(13.4, "-RPGAutoSelect=thief")
    "04-select-knight"= @(11.5, "-RPGAutoSelect=knight")
    "05-village-day"  = @(9,    "-RPGClass=knight -RPGHour=13")
    "06-golden-hour"  = @(9,    "-RPGClass=mage -RPGSex=female -RPGHour=18.6")
    "07-deep-night"   = @(9,    "-RPGClass=thief -RPGHour=22.5")
    "08-bridge-duel"  = @(4.5,  "-RPGClass=knight -RPGTest=bridge")
    "09-arrow-flight" = @(1.25, "-RPGClass=thief -RPGTest=thief")
    "10-conversation" = @(6,    "-RPGClass=knight -RPGTest=elder", "elder")
    "11-ability-pick" = @(1.0,  "-RPGClass=mage -RPGTest=picker")
}
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$names = @(foreach ($k in $shots.Keys) { if ($Only.Count -eq 0 -or @($Only | Where-Object { $k -like "*$_*" }).Count) { $k } })

$queue = [System.Collections.Generic.Queue[string]]::new([string[]]$names)
$running = @{}
Write-Host "Capturing $($names.Count) screenshots, $Parallel at a time..."
while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    while ($queue.Count -gt 0 -and $running.Count -lt $Parallel) {
        $name = $queue.Dequeue()
        $at, $extra, $png = $shots[$name]
        Remove-Item (Join-Path $ShotDir "$(if ($png) { $png } else { $name }).png") -ErrorAction SilentlyContinue
        $a = @("`"$Proj`"", "-game", "-windowed", "-ResX=1600", "-ResY=900", "-log", "-abslog=`"$(Join-Path $LogDir "$name.log")`"",
               "-RPGNoInput", "-RPGShot=$at", "-RPGShotName=$name") + ($extra -split " " | Where-Object { $_ })
        $running[$name] = Start-Process -FilePath $Editor -ArgumentList $a -PassThru
    }
    foreach ($n in @($running.Keys)) { if ($running[$n].HasExited) { $running.Remove($n) } }
    Start-Sleep -Milliseconds 300
}

Add-Type -AssemblyName System.Drawing
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$params = New-Object System.Drawing.Imaging.EncoderParameters 1
$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality, [long]88)
foreach ($name in $names) {
    $png = Join-Path $ShotDir "$(if ($shots[$name].Count -gt 2) { $shots[$name][2] } else { $name }).png"
    if (-not (Test-Path $png)) { Write-Host "  missing: $name" -ForegroundColor Red; continue }
    $img = [System.Drawing.Image]::FromFile($png)
    $jpg = Join-Path $OutDir "$name.jpg"
    $img.Save($jpg, $codec, $params)
    $img.Dispose()
    Write-Host ("  {0,-18} {1:n0} KB" -f $name, ((Get-Item $jpg).Length / 1KB))
}
