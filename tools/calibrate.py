"""Absolute strength calibration vs Stockfish UCI_LimitStrength.

Plays color-swapped game pairs against Stockfish locked at several UCI_Elo
levels, then max-likelihood-fits the Elo where the expected score crosses 50%.

Note: Stockfish's UCI_Elo is calibrated at longer time controls; at fast
movetime this yields an approximation, not a CCRL-equivalent rating.

Usage:
    python tools/calibrate.py <our.exe> <stockfish.exe> [games_per_level] [movetime_ms] [workers]
"""
import math
import random
import sys
from concurrent.futures import ProcessPoolExecutor

import chess
import chess.engine

LEVELS = [1700, 2000, 2300, 2600, 2900]
MAX_PLIES = 240


def random_opening(seed):
    rng = random.Random(seed)
    board = chess.Board()
    for _ in range(6):
        moves = list(board.legal_moves)
        if not moves:
            return random_opening(seed + 10007)
        board.push(rng.choice(moves))
    if board.is_game_over():
        return random_opening(seed + 10007)
    return [m.uci() for m in board.move_stack]


def play_game(args):
    our_exe, sf_exe, level, opening, we_are_white, movetime = args
    limit = chess.engine.Limit(time=movetime)
    engines = {}
    try:
        ours = chess.engine.SimpleEngine.popen_uci(our_exe)
        ours.configure({"Hash": 128, "Threads": 1})
        engines["us"] = ours

        sf = chess.engine.SimpleEngine.popen_uci(sf_exe)
        sf.configure({"UCI_LimitStrength": True, "UCI_Elo": level,
                      "Hash": 64, "Threads": 1})
        engines["sf"] = sf

        board = chess.Board()
        for uci in opening:
            board.push_uci(uci)

        white = "us" if we_are_white else "sf"
        black = "sf" if we_are_white else "us"

        while not board.is_game_over(claim_draw=True) and board.ply() < MAX_PLIES:
            side = white if board.turn == chess.WHITE else black
            result = engines[side].play(board, limit)
            if result.move is None:
                raise RuntimeError(f"{side} returned no move")
            board.push(result.move)

        outcome = board.outcome(claim_draw=True)
        if outcome is None or outcome.winner is None:
            return (level, 0.5)
        winner = white if outcome.winner == chess.WHITE else black
        return (level, 1.0 if winner == "us" else 0.0)
    finally:
        for eng in engines.values():
            try:
                eng.quit()
            except Exception:
                pass


def fit_elo(results):
    """Max-likelihood Elo over all games: P(win-equivalent) via logistic."""
    best_e, best_nll = None, float("inf")
    for e10 in range(12000, 36000):  # 1200.0 .. 3600.0 in 0.1 steps
        e = e10 / 10.0
        nll = 0.0
        for level, s in results:
            p = 1.0 / (1.0 + 10.0 ** ((level - e) / 400.0))
            p = min(max(p, 1e-9), 1 - 1e-9)
            nll -= s * math.log(p) + (1.0 - s) * math.log(1.0 - p)
        if nll < best_nll:
            best_nll, best_e = nll, e
    return best_e


def main():
    our_exe, sf_exe = sys.argv[1], sys.argv[2]
    per_level = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    movetime = (int(sys.argv[4]) if len(sys.argv) > 4 else 150) / 1000.0
    workers = int(sys.argv[5]) if len(sys.argv) > 5 else 8

    per_level -= per_level % 2
    tasks = []
    for level in LEVELS:
        for pair in range(per_level // 2):
            opening = random_opening(9000 + level * 31 + pair)
            tasks.append((our_exe, sf_exe, level, opening, True, movetime))
            tasks.append((our_exe, sf_exe, level, opening, False, movetime))

    results = []
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for i, r in enumerate(pool.map(play_game, tasks)):
            results.append(r)
            if (i + 1) % 20 == 0:
                print(f"{i + 1}/{len(tasks)} games", flush=True)

    print()
    for level in LEVELS:
        scores = [s for (lv, s) in results if lv == level]
        if scores:
            avg = sum(scores) / len(scores)
            w = scores.count(1.0)
            d = scores.count(0.5)
            l = scores.count(0.0)
            print(f"vs SF {level}: +{w} ={d} -{l}  score {avg:.3f}")

    est = fit_elo(results)
    n = len(results)
    # rough CI via +-1 game-score jitter at the anchor level
    print(f"\nEstimated Elo (this time control, {n} games): ~{est:.0f}")


if __name__ == "__main__":
    main()
