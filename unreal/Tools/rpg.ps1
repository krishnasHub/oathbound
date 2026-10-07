<#
  Developer commands for the Unreal version of the action RPG, on Tessera's tools
  (Plugins/Tessera/Tools/Tessera.ps1, configured by ../tessera.json).

    .\Tools\rpg.ps1 build                 compile the C++ (needed after any code change)
    .\Tools\rpg.ps1 run                   play (windowed). Add -Class mage -Sex female to skip character select
    .\Tools\rpg.ps1 editor                open the project in the Unreal Editor
    .\Tools\rpg.ps1 prepare               regenerate the materials and pixel textures (Content/RPG)
    .\Tools\rpg.ps1 art                   redraw the pixel art (tools/pixelart -> ImportSource/Pixel); then prepare
    .\Tools\rpg.ps1 shot -Name village -Cam "2100,3600,900,-18,-70"   screenshot from a fixed camera, then quit
    .\Tools\rpg.ps1 sync                  copy data/game-data.json into this project and the HTML prototype
    .\Tools\rpg.ps1 test -Scenario walk   one self-test scenario (or several: walk,click), with its log lines
    .\Tools\rpg.ps1 test -Scenario pose -Class thief   pose screenshots -> Saved/Screenshots/RPG/pose_*.png
#>
param(
    [Parameter(Position = 0)] [ValidateSet("build", "run", "editor", "prepare", "art", "shot", "sync", "test")] [string] $Command = "run",
    [string] $Class = "",
    [string] $Sex = "",
    [string] $Name = "shot",
    [string] $Cam = "",
    [double] $At = 25,
    [string] $Extra = "",
    [string] $Scenario = "combat"
)

$tessera = Join-Path $PSScriptRoot "..\Plugins\Tessera\Tools\Tessera.ps1"
$config = Join-Path $PSScriptRoot "..\..\tessera.json"
$extraArgs = @()
if ($Class) { $extraArgs += "-RPGClass=$Class" }
if ($Sex) { $extraArgs += "-RPGSex=$Sex" }
if ($Extra) { $extraArgs += ($Extra -split " " | Where-Object { $_ }) }

switch ($Command) {
    "run"  { & $tessera -Config $config play -Windowed -GameArgs $extraArgs }
    "shot" { & $tessera -Config $config shot -Name $Name -At $At -Cam $Cam -GameArgs $extraArgs }
    "test" { & $tessera -Config $config test -Scenario $Scenario -GameArgs $extraArgs }
    default { & $tessera -Config $config $Command }
}
exit $LASTEXITCODE
