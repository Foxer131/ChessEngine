# SPRT testing harness

Measures whether a change to the engine is a **real** strength improvement, by
playing many fast games of `patch` (your change) vs `base` (a committed ref) and
applying a Sequential Probability Ratio Test. This is the only honest way to
confirm "significant performance change" before keeping a strength tweak.

## One-time setup: get a match runner

You need **fastchess** (recommended) or **cutechess-cli**. They share the same
CLI flags, so either works with `sprt.ps1`.

- **fastchess** (single portable exe, no install): download a Windows release
  from <https://github.com/Disservin/fastchess/releases> and unzip somewhere,
  e.g. `C:\tools\fastchess.exe`.
- **cutechess-cli**: `winget install CuteChess.CuteChess` (the install dir holds
  `cutechess-cli.exe`).

## Run a test

```powershell
# 1. Build both binaries: patch = your working tree, base = last commit.
pwsh tools\build_versions.ps1                  # or -BaseRef <sha> to pick the base

# 2. Play the SPRT match.
pwsh tools\sprt.ps1 -Matcher C:\tools\fastchess.exe
```

`build_versions.ps1` builds only the `engine` target (no Qt/moc), and builds the
base from a throwaway git worktree under `C:\chess_sprt`, so your working tree is
never touched. Binaries go to `tools\bin\{patch,base}\engine.exe`.

## Reading the result

The runner prints a running **LLR** (log-likelihood ratio) with two bounds:

- `LLR >= 2.94`  → **H1 accepted**: the patch is a genuine improvement (keep it).
- `LLR <= -2.94` → **H0 accepted**: the patch is *not* an improvement (revert it).
- in between     → not enough evidence yet; keep playing.

It also prints Elo ± an error bar and the W-L-D count. Defaults test the
hypothesis `[elo0=0, elo1=5]` at `alpha=beta=0.05` ("is this a >0 Elo gain?").
For a change expected to be large, widen with `-Elo1 10`; for a tiny tweak,
`[0,3]` needs more games but is more sensitive.

## Notes / gotchas

- **TC**: default `8+0.08` (8 s + 0.08 s/move). Faster TC = more games/hour but
  noisier per game; this is the usual regression-testing range. Use a longer TC
  (`-Tc 20+0.2`) for a final confirmation.
- **OwnBook is forced off** in the match so the engine's built-in opening book
  can't override `openings.pgn` (and so games actually vary by opening).
- **Concurrency** defaults to (cores - 1). Don't max it out or the time control
  becomes unreliable (engines starve for CPU and lose on time spuriously).
- `openings.pgn` is a small set of ~20 mainstream openings; the runner plays the
  first 8 plies of a random line, then the engines are on their own. Swap in a
  bigger book (e.g. UHO / Pohl sets) for less opening bias on long runs.
- **Non-ASCII repo path:** the match runner can't open files under this repo's
  path (the accented `Joao`), so `sprt.ps1` stages the engines + book under
  `C:\chess_sprt\run` and runs from there. Don't move the staging to the repo.
- **MinGW DLLs:** `sprt.ps1` prepends `C:\msys64\mingw64\bin` to PATH so the
  dynamically-linked engine can load libstdc++/libgcc/winpthread; otherwise the
  child engines fail to start and the match aborts with "no uciok".
