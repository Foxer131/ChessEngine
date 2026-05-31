# =============================================================================
# sprt.ps1 - run an SPRT match between tools\bin\patch and tools\bin\base using
# fastchess (recommended, single portable exe) or cutechess-cli. Both share the
# same CLI flags, so pass whichever you have via -Matcher.
#
#   powershell -File tools\sprt.ps1 -Matcher C:\tools\fastchess\...\fastchess.exe
#   powershell -File tools\sprt.ps1 -Matcher cutechess-cli.exe -Tc "10+0.1" -Elo1 8
#
# SPRT stops automatically when it can accept H1 (patch is elo1 better) or H0
# (patch is not elo0 better). Defaults test [elo0=0, elo1=5] at alpha=beta=0.05:
# "is the patch a real, >0 Elo improvement?". OwnBook is forced OFF so the engine's
# built-in book can't override the opening set (and so games actually vary).
#
# NOTE: this machine's repo path contains a non-ASCII char (Joao) which the match
# runner can't open. So everything the runner touches (engine exes, opening book,
# pgn output) is staged under an ASCII dir, C:\chess_sprt\run, and the match runs
# from there. The repo-tracked scripts/book stay where they are.
# =============================================================================
param(
    [Parameter(Mandatory = $true)][string]$Matcher,
    [string]$Tc          = "8+0.08",   # seconds + increment per move (fast = many games)
    [int]   $Rounds      = 4000,
    [double]$Elo0        = 0,
    [double]$Elo1        = 5,
    [int]   $Concurrency = 0            # 0 = auto (cores - 1, capped at 8)
)
$ErrorActionPreference = "Continue"
# The MinGW-built engine is dynamically linked to libstdc++/libgcc/winpthread;
# put MSYS2 on PATH so fastchess's child engine processes can load those DLLs
# (without this they fail to start and the match aborts with "no uciok").
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
$tools = $PSScriptRoot

if ($Concurrency -le 0) {
    $Concurrency = [Math]::Min(8, [Environment]::ProcessorCount - 1)
    if ($Concurrency -lt 1) { $Concurrency = 1 }
}

$srcPatch = Join-Path $tools "bin\patch\engine.exe"
$srcBase  = Join-Path $tools "bin\base\engine.exe"
$srcBook  = Join-Path $tools "openings\openings.pgn"
foreach ($f in @($srcPatch, $srcBase, $srcBook)) {
    if (-not (Test-Path $f)) { Write-Error "Missing $f - run tools\build_versions.ps1 first."; exit 1 }
}

# Stage into an ASCII path the runner can actually open.
$stage = "C:\chess_sprt\run"
New-Item -ItemType Directory -Force $stage -ErrorAction Stop | Out-Null
$patch = Join-Path $stage "patch.exe"
$base  = Join-Path $stage "base.exe"
$book  = Join-Path $stage "openings.pgn"
$pgn   = Join-Path $stage "sprt.pgn"
Copy-Item $srcPatch $patch -Force -ErrorAction Stop
Copy-Item $srcBase  $base  -Force -ErrorAction Stop
Copy-Item $srcBook  $book  -Force -ErrorAction Stop

Write-Host "patch vs base | TC=$Tc | concurrency=$Concurrency | SPRT[$Elo0,$Elo1] | staged in $stage" -ForegroundColor Cyan

& $Matcher `
    -engine cmd="$patch" name=patch `
    -engine cmd="$base"  name=base `
    -each proto=uci tc=$Tc option.OwnBook=false `
    -openings file="$book" format=pgn order=random plies=8 `
    -games 2 -rounds $Rounds -repeat -recover `
    -sprt elo0=$Elo0 elo1=$Elo1 alpha=0.05 beta=0.05 `
    -concurrency $Concurrency `
    -ratinginterval 20 `
    -pgnout file="$pgn"
