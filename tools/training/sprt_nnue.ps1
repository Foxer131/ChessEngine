# =============================================================================
# sprt_nnue.ps1 - SPRT of (engine + NNUE) vs (same engine, HCE). Same binary on
# both sides; the only difference is the patch loads EvalFile.
#
#   powershell -File tools\training\sprt_nnue.ps1 -Net C:\chess_sprt\data\net.nnue
#   powershell -File tools\training\sprt_nnue.ps1 -Net ... -Nodes 20000   # fixed-nodes
#
# Use -Nodes to isolate NET QUALITY from inference speed (NNUE is ~3x slower than
# HCE; a wall-clock TC would conflate "weaker net" with "fewer nodes searched").
# With -Nodes>0 both sides search the same node budget per move. -Nodes 0 (default)
# uses the wall-clock TC instead.
# =============================================================================
param(
    [Parameter(Mandatory=$true)][string]$Net,
    [int]   $Nodes       = 20000,        # 0 => use TC instead
    [string]$Tc          = "5+0.05",
    [int]   $Rounds      = 1000,
    [double]$Elo0        = 0,
    [double]$Elo1        = 10,
    [int]   $Concurrency = 8
)
$ErrorActionPreference = "Continue"
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path

$fc  = "C:\tools\fastchess\fastchess-windows-x86-64\fastchess.exe"
$s   = "C:\chess_sprt\run"
New-Item -ItemType Directory -Force $s | Out-Null
Copy-Item "C:\chess_build\bin\engine.exe" "$s\eng.exe" -Force
Copy-Item $Net "$s\net.nnue" -Force

# Per-side budget: fixed nodes (isolates net quality) or the wall-clock TC.
if ($Nodes -gt 0) { $each = @("proto=uci","nodes=$Nodes","option.OwnBook=false") }
else              { $each = @("proto=uci","tc=$Tc","option.OwnBook=false") }

Write-Host ("NNUE vs HCE | {0} | SPRT[{1},{2}]" -f `
    ($(if($Nodes -gt 0){"nodes=$Nodes"}else{"tc=$Tc"})), $Elo0, $Elo1) -ForegroundColor Cyan

& $fc -engine cmd="$s\eng.exe" name=nnue option.OwnBook=false "option.EvalFile=$s\net.nnue" `
      -engine cmd="$s\eng.exe" name=hce  option.OwnBook=false `
      -each @each `
      -openings file="$s\openings.pgn" format=pgn order=random plies=8 `
      -games 2 -rounds $Rounds -repeat -recover `
      -sprt elo0=$Elo0 elo1=$Elo1 alpha=0.05 beta=0.05 `
      -concurrency $Concurrency -ratinginterval 20 `
      -pgnout file="$s\nnue_games.pgn"
