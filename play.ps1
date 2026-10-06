<#
.SYNOPSIS
  One-click launcher for the Action RPG (Unreal Engine 5.8).

.DESCRIPTION
  Does whatever is needed, then starts the game:
    1. finds Unreal Engine 5.8
    2. syncs data/game-data.json into the game (if you edited it)
    3. compiles the C++ (only if the code changed since the last build)
    4. generates the game's materials (only the first time)
    5. launches the game full-screen, like a normal game

.EXAMPLE
  .\play.ps1                          # play (full-screen, starts at character select)
  .\play.ps1 -Windowed                # play in a 1600x900 window
  .\play.ps1 -Class mage -Sex female  # skip character select
  .\play.ps1 -Test                    # run every automated scenario (as many at once as this PC allows; -Parallel N) and report PASS/FAIL
  .\play.ps1 -Package                 # build a standalone game (Dist\Windows\ActionRPG.exe) and run it
  .\play.ps1 -Rebuild                 # force a full recompile first
#>
param(
    [switch] $Windowed,
    [string] $Class = "",
    [string] $Sex = "",
    [switch] $Test,
    [switch] $Package,
    [switch] $Rebuild,
    [int] $Parallel = 0        # -Test: scenarios running at once (0 = decide from this PC; 1 = one by one)
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$ProjDir = Join-Path $Root "unreal"
$Proj = Join-Path $ProjDir "ActionRPG.uproject"
$Log = Join-Path $ProjDir "Saved\Logs\ActionRPG.log"

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Note($msg) { Write-Host "    $msg" -ForegroundColor DarkGray }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# ------------------------------------------------------------------------------------------------
# 1. Unreal Engine 5.8
# ------------------------------------------------------------------------------------------------
Step "Finding Unreal Engine 5.8"
$UE = $null
$launcherDat = "C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
if (Test-Path $launcherDat) {
    $installs = (Get-Content $launcherDat -Raw | ConvertFrom-Json).InstallationList
    foreach ($i in $installs) { if ($i.AppName -eq "UE_5.8") { $UE = $i.InstallLocation } }
}
if (-not $UE) { $UE = "C:\Program Files\Epic Games\UE_5.8" }
$Editor = Join-Path $UE "Engine\Binaries\Win64\UnrealEditor.exe"
if (-not (Test-Path $Editor)) { Fail "Unreal Engine 5.8 not found (looked in $UE). Install it from the Epic Games Launcher." }
Note $UE

# ------------------------------------------------------------------------------------------------
# 2. Game data
# ------------------------------------------------------------------------------------------------
$dataSrc = Join-Path $Root "data\game-data.json"
$dataDst = Join-Path $ProjDir "Content\Data\game-data.json"
if (-not (Test-Path $dataDst) -or (Get-Item $dataSrc).LastWriteTime -gt (Get-Item $dataDst).LastWriteTime) {
    Step "Syncing game data"
    if (Get-Command node -ErrorAction SilentlyContinue) {
        node (Join-Path $Root "tools\sync-data.js")
        if ($LASTEXITCODE -ne 0) { Fail "data/game-data.json is not valid JSON - fix it and try again." }
    } else {
        # No Node: copy straight across (the HTML prototype just won't be updated).
        Copy-Item $dataSrc $dataDst -Force
        Note "Node.js not found: copied the data into the game only (HTML prototype not updated)."
    }
}

# ------------------------------------------------------------------------------------------------
# 3. Compile the C++ if it changed
# ------------------------------------------------------------------------------------------------
$dll = Join-Path $ProjDir "Binaries\Win64\UnrealEditor-ActionRPG.dll"
$needBuild = $Rebuild -or -not (Test-Path $dll)
if (-not $needBuild) {
    $built = (Get-Item $dll).LastWriteTime
    $changed = Get-ChildItem (Join-Path $ProjDir "Source") -Recurse -File | Where-Object { $_.LastWriteTime -gt $built } | Select-Object -First 1
    if ($changed -or (Get-Item $Proj).LastWriteTime -gt $built) { $needBuild = $true }
}
if ($needBuild) {
    Step "Compiling the game code (first time: a few minutes; after that: seconds)"
    $buildArgs = @("ActionRPGEditor", "Win64", "Development", "-Project=`"$Proj`"", "-WaitMutex")
    if ($Rebuild) { $buildArgs += "-Clean" }
    & (Join-Path $UE "Engine\Build\BatchFiles\Build.bat") @buildArgs | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Fail "Compile failed. You need Visual Studio 2026 with the 'Game development with C++' workload. Errors are above."
    }
} else { Note "Code is up to date." }

# ------------------------------------------------------------------------------------------------
# 4. Generated assets (materials) - first run only
# ------------------------------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $ProjDir "Content\RPG\Materials\M_RPG_Glow.uasset")) -or -not (Test-Path (Join-Path $ProjDir "Content\RPG\Materials\M_RPG_Sprite.uasset"))) {
    Step "Generating game materials (first run only)"
    $cmd = Join-Path $UE "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    foreach ($script in @("fix_material_usage.py", "create_materials.py", "import_pixel.py")) {
        & $cmd "$Proj" -run=pythonscript -script="$(Join-Path $ProjDir "Tools\$script")" -unattended -nosplash -nullrhi | Out-Null
    }
}

# ------------------------------------------------------------------------------------------------
# Test mode: every scenario, PASS / FAIL
# ------------------------------------------------------------------------------------------------
if ($Test) {
    # Each scenario is its own game process with its own log (-abslog), so several run at once.
    $scenarios = [ordered]@{ combat = "knight"; block = "knight"; elder = "knight"; bridge = "knight"; mage = "mage"; thief = "thief"; walk = "knight"; picker = "mage"; smoke = "thief"; pause = "knight"; click = "knight" }
    $logDir = Join-Path $ProjDir "Saved\Logs\Tests"
    New-Item -ItemType Directory -Force $logDir | Out-Null
    if ($Parallel -le 0) {
        # Size the batch to this PC: each game instance needs roughly 3 CPU cores, 2.5 GB of RAM (keeping 4 GB for
        # Windows) and 2 GB of video memory. Whichever runs out first sets the limit.
        $cores = (Get-CimInstance Win32_Processor | Measure-Object NumberOfCores -Sum).Sum
        $freeGB = [math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB, 1)
        $vramGB = 0
        try {
            $vramGB = (Get-ItemProperty "HKLM:\SYSTEM\ControlSet001\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0*" -Name "HardwareInformation.qwMemorySize" -ErrorAction Stop |
                ForEach-Object { $_."HardwareInformation.qwMemorySize" } | Measure-Object -Maximum).Maximum / 1GB
        } catch {}
        if ($vramGB -le 0) { $vramGB = 4 }   # unknown: assume a modest card
        $byCpu = [math]::Floor($cores / 3)
        $byRam = [math]::Floor(($freeGB - 4) / 2.5)
        $byGpu = [math]::Floor($vramGB / 2)
        $Parallel = [math]::Max(1, [math]::Min([math]::Min($byCpu, $byRam), [math]::Min($byGpu, $scenarios.Count)))
        Note ("This PC: {0} cores, {1} GB RAM free, {2:n0} GB video memory -> CPU allows {3}, RAM {4}, GPU {5}: running {6} at a time" -f $cores, $freeGB, $vramGB, $byCpu, $byRam, $byGpu, $Parallel)
    }
    $started = Get-Date
    $queue = [System.Collections.Generic.Queue[string]]::new([string[]]$scenarios.Keys)
    $running = @{}
    Step "Running $($scenarios.Count) scenarios, $Parallel at a time"
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($queue.Count -gt 0 -and $running.Count -lt $Parallel) {
            $name = $queue.Dequeue()
            $log = Join-Path $logDir "$name.log"
            Remove-Item $log -ErrorAction SilentlyContinue
            $a = @("`"$Proj`"", "-game", "-windowed", "-ResX=960", "-ResY=540", "-log", "-abslog=`"$log`"", "-RPGNoInput", "-RPGTest=$name", "-RPGClass=$($scenarios[$name])")
            $running[$name] = @{ Proc = (Start-Process -FilePath $Editor -ArgumentList $a -PassThru); Start = Get-Date }
        }
        foreach ($name in @($running.Keys)) {
            $r = $running[$name]
            if ($r.Proc.HasExited) { $running.Remove($name); Note ("{0,-8} finished in {1:n0}s" -f $name, ((Get-Date) - $r.Start).TotalSeconds) }
            elseif (((Get-Date) - $r.Start).TotalSeconds -gt 240) { Stop-Process -Id $r.Proc.Id -Force; $running.Remove($name); Note "$name timed out" }
        }
        Start-Sleep -Milliseconds 300
    }
    $results = @()
    foreach ($name in $scenarios.Keys) {
        $log = Join-Path $logDir "$name.log"
        $lines = @()
        if (Test-Path $log) { $lines = Select-String -Path $log -Pattern "\[TEST $name" | ForEach-Object { $_.Line -replace "^\[.*?\]\[.*?\]LogRPG: Display: ", "" } }
        $ok = ($lines | Where-Object { $_ -match "\] done" }).Count -gt 0 -and ($lines | Where-Object { $_ -match "FAIL" }).Count -eq 0
        if (-not $ok) { Write-Host "  --- $name ---" -ForegroundColor DarkGray; $lines | ForEach-Object { Note $_ } }
        $results += [pscustomobject]@{ Scenario = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }) }
    }
    Write-Host ""
    foreach ($r in $results) {
        $color = "Green"; if ($r.Result -ne "PASS") { $color = "Red" }
        Write-Host ("  {0,-8} {1}" -f $r.Scenario, $r.Result) -ForegroundColor $color
    }
    Write-Host ("  all done in {0:n0}s (logs: {1})" -f ((Get-Date) - $started).TotalSeconds, $logDir)
    if ($results | Where-Object { $_.Result -ne "PASS" }) { exit 1 }
    exit 0
}

# ------------------------------------------------------------------------------------------------
# Package mode: a standalone game in Dist\
# ------------------------------------------------------------------------------------------------
$extra = @()
if ($Class) { $extra += "-RPGClass=$Class" }
if ($Sex) { $extra += "-RPGSex=$Sex" }
$display = @("-fullscreen")
if ($Windowed) { $display = @("-windowed", "-ResX=1600", "-ResY=900") }

if ($Package) {
    $exe = Join-Path $Root "Dist\Windows\ActionRPG.exe"
    Step "Packaging a standalone build (first time: 10-20 minutes)"
    & (Join-Path $UE "Engine\Build\BatchFiles\RunUAT.bat") BuildCookRun -project="$Proj" -noP4 -platform=Win64 `
        -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="$(Join-Path $Root 'Dist')" -unattended -utf8output | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) { Fail "Packaging failed - see the output above." }
    Step "Starting $exe"
    Start-Process -FilePath $exe -ArgumentList ($display + $extra)
    exit 0
}

# ------------------------------------------------------------------------------------------------
# Play
# ------------------------------------------------------------------------------------------------
Step "Starting the game (Alt+F4 to quit, H in-game for controls)"
Start-Process -FilePath $Editor -ArgumentList (@("`"$Proj`"", "-game") + $display + $extra)
