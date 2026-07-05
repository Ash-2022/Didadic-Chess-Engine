# Didadic-Chess-Engine

UCI chess engine with magic-bitboard move generation, lazy-SMP search
(negamax + aspiration windows, TT, LMR, singular extensions, multicut,
correction history) and NNUE evaluation.

## NNUE

Evaluation uses an efficiently updatable neural network:
`(768 -> 256)x2 -> 1`, CReLU, int16-quantized (QA=255, QB=64, scale=400),
AVX2 inference with a scalar fallback. Both perspective accumulators are
updated incrementally inside `Position::makeMove/undoMove` via the
`addPieceEval`/`removePieceEval` hooks and rebuilt at every search root.

- Weights: `nnue_weights.bin` (~386 KB), loaded from the working directory
  or next to the executable. If missing/invalid, the engine falls back to
  the original hand-crafted evaluation.
- UCI options: `UseNNUE` (check, default true), `NNUEPath` (string).
- CLI: `--nnue PATH`, `--nonnue`, `--evalbench` (ns/eval benchmark).

## Build

```powershell
.\build.ps1 -Config ReleaseMax     # optimized build -> build/clang-release-max/chess_engine.exe
.\build.ps1 -Config Debug          # asserts incl. NNUE accumulator verification
```

Note: `build.ps1` kills running `chess_engine` processes before building.

## Training pipeline

```powershell
# 1. Generate self-play data (run several in parallel with different seeds)
.\build\clang-release-max\chess_engine.exe --datagen --nonnue `
    --dggames 2500 --dgdepth 8 --dgseed 1 --dgout data\dg_1.txt --threads 1 --hash 128

# 2. Train (MSYS2 python with numpy/scipy)
C:\msys64\mingw64\bin\python.exe tools\train_nnue.py "data\dg_*.txt" nnue_weights.bin 40 256

# 3. Measure strength vs the hand-crafted eval (needs python-chess)
C:\msys64\mingw64\bin\python.exe tools\match.py build\clang-release-max\chess_engine.exe 200 150 6

# 4. Depth-20 validation (timing + PV legality)
C:\msys64\mingw64\bin\python.exe tools\depth20_check.py build\clang-release-max\chess_engine.exe
```

Datagen records quiet positions as `fen | white-POV score | white-POV result`;
the trainer optimizes sigmoid-MSE on a 0.75/0.25 blend of search score and
game result, then quantizes to the engine's binary format (see
`Engine_Core/src/nnue.h` for the exact layout and feature indexing).
