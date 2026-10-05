<#
  Build / run / test the Unreal version of the action RPG.

    .\Tools\rpg.ps1 build                 compile the C++ (needed after any code change)
    .\Tools\rpg.ps1 run                   play (windowed). Add -Class mage -Sex female to skip character select
    .\Tools\rpg.ps1 editor                open the project in the Unreal Editor
    .\Tools\rpg.ps1 prepare               one-time asset fix-ups (material flags) — already done, rerun if Content changes
    .\Tools\rpg.ps1 shot -Name village -Cam "2100,3600,900,-18,-70"   screenshot from a fixed camera, then quit
    .\Tools\rpg.ps1 sync                  copy data/game-data.json into this project and the HTML prototype

  Requires Unreal Engine 5.8 at the path below (change $UE if yours lives elsewhere).
#>
param(
    [Parameter(Position = 0)] [ValidateSet("build", "run", "editor", "prepare", "shot", "sync", "test")] [string] $Command = "run",
    [string] $Class = "",
    [string] $Sex = "",
    [string] $Name = "shot",
    [string] $Cam = "",
    [int] $At = 25,
    [string] $Extra = "",
    [string] $Scenario = "combat"
)

$ErrorActionPreference = "Stop"
$UE = "C:\Program Files\Epic Games\UE_5.8"
$ProjDir = Split-Path -Parent $PSScriptRoot
$Proj = Join-Path $ProjDir "ActionRPG.uproject"
$Editor = Join-Path $UE "Engine\Binaries\Win64\UnrealEditor.exe"

function Game-Args {
    $a = @("`"$Proj`"", "-game", "-windowed", "-ResX=1600", "-ResY=900", "-log")
    if ($Class) { $a += "-RPGClass=$Class" }
    if ($Sex) { $a += "-RPGSex=$Sex" }
    if ($Extra) { $a += $Extra }
    return $a
}

switch ($Command) {
    "build" {
        & "$UE\Engine\Build\BatchFiles\Build.bat" ActionRPGEditor Win64 Development -Project="$Proj" -WaitMutex
        if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    }
    "run" {
        Start-Process -FilePath $Editor -ArgumentList (Game-Args)
    }
    "editor" {
        Start-Process -FilePath $Editor -ArgumentList "`"$Proj`""
    }
    "prepare" {
        foreach ($script in @("fix_material_usage.py", "create_materials.py")) {
            & "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" -run=pythonscript -script="$PSScriptRoot\$script" -unattended -nosplash -nullrhi
        }
    }
    "shot" {
        $a = (Game-Args) + @("-RPGShot=$At", "-RPGShotName=$Name")
        if ($Cam) { $a += "-RPGCam=$Cam" }
        $p = Start-Process -FilePath $Editor -ArgumentList $a -PassThru
        $p.WaitForExit()
        Write-Host "Screenshot: $ProjDir\Saved\Screenshots\RPG\$Name.png"
    }
    "test" {
        $cls = if ($Class) { $Class } else { @{ combat = "knight"; bridge = "knight"; mage = "mage"; thief = "thief"; elder = "knight"; block = "knight"; pose = "mage" }[$Scenario] }
        $a = @("`"$Proj`"", "-game", "-windowed", "-ResX=1280", "-ResY=720", "-log", "-RPGNoInput", "-RPGTest=$Scenario", "-RPGClass=$cls")
        if ($At -ne 25) { $a += @("-RPGShot=$At", "-RPGShotName=$Scenario") }
        $p = Start-Process -FilePath $Editor -ArgumentList $a -PassThru
        $p.WaitForExit()
        Select-String -Path (Join-Path $ProjDir "Saved\Logs\ActionRPG.log") -Pattern "\[TEST" | ForEach-Object { $_.Line -replace "^\[.*?\]\[.*?\]LogRPG: Display: ", "" }
    }
    "sync" {
        node (Join-Path (Split-Path -Parent $ProjDir) "tools\sync-data.js")
    }
}
