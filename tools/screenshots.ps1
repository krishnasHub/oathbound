<#
  Takes the README gallery: the game moments listed in tessera.json (shots), captured in parallel and saved as
  JPEGs in docs/screenshots/ (Tessera's tools: unreal/Plugins/Tessera/Tools/Tessera.ps1 shots).

    .\tools\screenshots.ps1            # all shots
    .\tools\screenshots.ps1 -Only dusk,night
#>
param([string[]] $Only = @(), [int] $Parallel = 4)

$Root = Split-Path -Parent $PSScriptRoot
& (Join-Path $Root "unreal\Plugins\Tessera\Tools\Tessera.ps1") -Config (Join-Path $Root "tessera.json") shots -Only $Only -Parallel $Parallel
exit $LASTEXITCODE
