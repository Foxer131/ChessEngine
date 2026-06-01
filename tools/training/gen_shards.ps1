# =============================================================================
# gen_shards.ps1 - generate NNUE training data in parallel shards, then merge.
# Each shard is an independent gen_data run with its own seed + output file
# (deterministic, no shared state), so we just launch N and concatenate.
#
#   powershell -File tools\training\gen_shards.ps1 -Shards 12 -Games 290 -Nodes 5000
#
# Output: C:\chess_sprt\data\all.txt  (plus per-shard files shardNN.txt)
# =============================================================================
param(
    [int]$Shards = 12,
    [int]$Games  = 290,    # games per shard (~84k positions each at 5000 nodes)
    [int]$Nodes  = 5000
)
$ErrorActionPreference = "Stop"
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path

$exe  = "C:\chess_build\bin\gen_data.exe"
$data = "C:\chess_sprt\data"
New-Item -ItemType Directory -Force $data | Out-Null
if (-not (Test-Path $exe)) { throw "gen_data not built: $exe" }

Write-Host "Launching $Shards shards x $Games games @ $Nodes nodes..." -ForegroundColor Cyan
$procs = @()
for ($i = 1; $i -le $Shards; $i++) {
    $out = Join-Path $data ("shard{0:D2}.txt" -f $i)
    $procs += Start-Process -FilePath $exe -ArgumentList @($out, $Games, $Nodes, $i) `
                            -NoNewWindow -PassThru
}
Write-Host "Waiting for $($procs.Count) shards to finish..." -ForegroundColor Cyan
$procs | Wait-Process

$all = Join-Path $data "all.txt"
Get-Content (Join-Path $data "shard*.txt") | Set-Content $all
$n = (Get-Content $all).Count
Write-Host ("Done: {0:N0} positions -> {1}" -f $n, $all) -ForegroundColor Green
