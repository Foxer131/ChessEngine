# =============================================================================
# retrain.ps1 - one-shot NNUE retrain pipeline:
#   normalize result tokens -> convert to bulletformat -> train (bullet+CUDA)
# Assumes data/all.txt already generated (tools/training/gen_shards.ps1).
#
#   powershell -File tools\training\retrain.ps1
#
# Output: the trained net at C:\chess_nnue\bullet\checkpoints\chessengine-<N>\quantised.bin
# (copy to C:\chess_sprt\data\net.nnue for the engine's EvalFile).
# =============================================================================
param(
    [string]$DataDir   = "C:\chess_sprt\data",
    [string]$BulletDir = "C:\chess_nnue\bullet"
)
$ErrorActionPreference = "Continue"
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
$env:CUDA_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
$env:Path = "$env:CUDA_PATH\bin;" + $env:Path

$repo  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # tools\training\.. \..
$py    = (Get-Command python).Source
$utils = Join-Path $BulletDir "target\release\bullet-utils.exe"

Write-Host "1/3 normalize result tokens" -ForegroundColor Cyan
& $py (Join-Path $PSScriptRoot "normalize_results.py") `
      (Join-Path $DataDir "all.txt") (Join-Path $DataDir "train_norm.txt")

Write-Host "2/3 convert text -> bulletformat" -ForegroundColor Cyan
& $utils convert --from text --input (Join-Path $DataDir "train_norm.txt") `
                 --output (Join-Path $DataDir "train.data")
Copy-Item (Join-Path $DataDir "train.data") (Join-Path $BulletDir "data\train.data") -Force

Write-Host "3/3 train (bullet + CUDA)" -ForegroundColor Cyan
Push-Location $BulletDir
cargo r -r --features cuda --example chessengine
Pop-Location

Write-Host "Done. Newest checkpoint:" -ForegroundColor Green
Get-ChildItem (Join-Path $BulletDir "checkpoints") -Directory |
    Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName
