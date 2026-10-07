<#
.SYNOPSIS
  One-click launcher for the Action RPG (Unreal Engine 5.8), on Tessera's tools.

.DESCRIPTION
  Does whatever is needed, then starts the game (unreal/Plugins/Tessera/Tools/Tessera.ps1, configured by
  tessera.json):
    1. finds Unreal Engine 5.8
    2. syncs data/game-data.json into the game (if you edited it)
    3. compiles the C++ (only if the game's or a plugin's code changed since the last build)
    4. generates the game's materials and textures (only the first time)
    5. launches the game full-screen, like a normal game

.EXAMPLE
  .\play.ps1                          # play (full-screen, starts at the title)
  .\play.ps1 -Windowed                # play in a 1600x900 window
  .\play.ps1 -Class mage -Sex female  # skip character select
  .\play.ps1 -Test                    # run every automated scenario (as many at once as this PC allows; -Parallel N) and report PASS/FAIL
  .\play.ps1 -Test -Scenario walk     # just one (or a few: walk,click)
  .\play.ps1 -Package                 # build a standalone game (Dist\Windows\ActionRPG.exe) and run it (-NoLaunch: just build)
  .\play.ps1 -Rebuild                 # force a full recompile first
#>
param(
    [switch] $Windowed,
    [string] $Class = "",
    [string] $Sex = "",
    [switch] $Test,
    [string[]] $Scenario = @(),
    [switch] $Package,
    [switch] $NoLaunch,
    [switch] $Rebuild,
    [int] $Parallel = 0        # -Test: scenarios running at once (0 = decide from this PC; 1 = one by one)
)

$tessera = Join-Path $PSScriptRoot "unreal\Plugins\Tessera\Tools\Tessera.ps1"
if (-not (Test-Path $tessera)) {
    Write-Host "ERROR: Tessera is missing. Run: git submodule update --init --recursive" -ForegroundColor Red
    exit 1
}
$extra = @()
if ($Class) { $extra += "-RPGClass=$Class" }
if ($Sex) { $extra += "-RPGSex=$Sex" }
$command = "play"
if ($Test) { $command = "test" } elseif ($Package) { $command = "package" }

& $tessera -Config (Join-Path $PSScriptRoot "tessera.json") $command -Windowed:$Windowed -Rebuild:$Rebuild -NoLaunch:$NoLaunch `
    -GameArgs $extra -Scenario $Scenario -Parallel $Parallel
exit $LASTEXITCODE
