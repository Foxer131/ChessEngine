# =============================================================================
# build_versions.ps1 - build two engine binaries for an A/B (SPRT) match:
#   patch = the current working tree (your uncommitted changes)
#   base  = a committed git ref (default HEAD = last commit)
# Both land in tools\bin\{patch,base}\engine.exe.
#
# Only the `engine` target is built, so Qt/moc is never invoked (no ASCII-path
# gotcha). The base is materialised with `git archive` into a throwaway dir under
# C:\chess_sprt (no git worktree, no .git locks), so your working tree and repo
# metadata are left completely untouched.
#
#   powershell -File tools\build_versions.ps1                 # patch=worktree, base=HEAD
#   powershell -File tools\build_versions.ps1 -BaseRef 6fc76b0
#   powershell -File tools\build_versions.ps1 -SkipBase       # rebuild only patch
# =============================================================================
param(
    [string]$BaseRef = "HEAD",
    [switch]$SkipBase
)
# Native tools (git, cmake) write progress to stderr; under -ErrorActionPreference
# Stop that would abort the script, so we stay on Continue and check exit codes.
$ErrorActionPreference = "Continue"
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path

function Assert-Ok([string]$what) {
    if ($LASTEXITCODE -ne 0) { Write-Error "$what failed (exit $LASTEXITCODE)"; exit 1 }
}

$root     = Split-Path -Parent $PSScriptRoot          # repo root (tools\..)
$binBase  = Join-Path $PSScriptRoot "bin\base"
$binPatch = Join-Path $PSScriptRoot "bin\patch"
New-Item -ItemType Directory -Force $binBase, $binPatch -ErrorAction Stop | Out-Null

$qt = "-DCMAKE_PREFIX_PATH=C:/msys64/mingw64"

Write-Host "==> Building PATCH (working tree)" -ForegroundColor Cyan
cmake -G Ninja -S $root -B C:/chess_build -DCMAKE_BUILD_TYPE=Release $qt | Out-Null
Assert-Ok "configure patch"
cmake --build C:/chess_build --target engine
Assert-Ok "build patch"
Copy-Item "C:/chess_build/bin/engine.exe" (Join-Path $binPatch "engine.exe") -Force -ErrorAction Stop
Write-Host "    -> tools\bin\patch\engine.exe" -ForegroundColor Green

if (-not $SkipBase) {
    $src = "C:\chess_sprt\base_src"
    $bd  = "C:\chess_sprt\base_build"
    $tar = "C:\chess_sprt\base.tar"
    Write-Host "==> Building BASE ($BaseRef) from a git-archive snapshot" -ForegroundColor Cyan
    New-Item -ItemType Directory -Force "C:\chess_sprt" -ErrorAction Stop | Out-Null
    if (Test-Path $src) { Remove-Item -Recurse -Force $src -ErrorAction Stop }
    New-Item -ItemType Directory -Force $src -ErrorAction Stop | Out-Null

    git -C $root archive --format=tar -o $tar $BaseRef
    Assert-Ok "git archive $BaseRef"
    tar -xf $tar -C $src
    Assert-Ok "tar extract"
    Remove-Item $tar -Force -ErrorAction SilentlyContinue

    cmake -G Ninja -S $src -B $bd -DCMAKE_BUILD_TYPE=Release $qt | Out-Null
    Assert-Ok "configure base"
    cmake --build $bd --target engine
    Assert-Ok "build base"
    Copy-Item "$bd\bin\engine.exe" (Join-Path $binBase "engine.exe") -Force -ErrorAction Stop
    Write-Host "    -> tools\bin\base\engine.exe" -ForegroundColor Green
}

Write-Host "Done. Now run: powershell -File tools\sprt.ps1 -Matcher <fastchess-or-cutechess-cli>" -ForegroundColor Yellow
