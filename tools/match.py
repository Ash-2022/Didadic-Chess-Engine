"""Self-play match: NNUE vs hand-crafted eval, same engine binary.

Plays paired games (color-swapped) from random openings at fixed movetime,
adjudicates with python-chess rules, and reports W/D/L + Elo difference.
Every move is legality-checked by python-chess (raises on illegal moves).

Usage:
    python tools/match.py <engine.exe> [games] [movetime_ms] [concurrency]
"""
import math
import random
import sys
from concurrent.futures import ProcessPoolExecutor

import chess
import chess.engine

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
    exe, opening, nnue_plays_white, movetime = args
    limit = chess.engine.Limit(time=movetime)

    engines = {}
    try:
        for label, use_nnue in (("nnue", True), ("hce", False)):
            eng = chess.engine.SimpleEngine.popen_uci(exe)
            eng.configure({"UseNNUE": use_nnue, "Hash": 128, "Threads": 1})
            engines[label] = eng

        board = chess.Board()
        for uci in opening:
            board.push_uci(uci)

        white = "nnue" if nnue_plays_white else "hce"
        black = "hce" if nnue_plays_white else "nnue"

        while not board.is_game_over(claim_draw=True) and board.ply() < MAX_PLIES:
            side = white if board.turn == chess.WHITE else black
            result = engines[side].play(board, limit)
            if result.move is None:
                raise RuntimeError(f"{side} returned no move")
            board.push(result.move)  # raises on illegal move

        outcome = board.outcome(claim_draw=True)
        if outcome is None or outcome.winner is None:
            return 0.5
        winner = white if outcome.winner == chess.WHITE else black
        return 1.0 if winner == "nnue" else 0.0
    finally:
        for eng in engines.values():
            try:
                eng.quit()
            except Exception:
                pass


def elo(s):
    s = min(max(s, 1e-6), 1 - 1e-6)
    return -400.0 * math.log10(1.0 / s - 1.0)


def main():
    exe = sys.argv[1]
    games = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    movetime = (int(sys.argv[3]) if len(sys.argv) > 3 else 150) / 1000.0
    workers = int(sys.argv[4]) if len(sys.argv) > 4 else 6

    games -= games % 2  # keep color-swapped pairs
    tasks = []
    for pair in range(games // 2):
        opening = random_opening(5000 + pair)
        tasks.append((exe, opening, True, movetime))
        tasks.append((exe, opening, False, movetime))

    scores = []
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for i, s in enumerate(pool.map(play_game, tasks)):
            scores.append(s)
            if (i + 1) % 20 == 0:
                n = len(scores)
                avg = sum(scores) / n
                print(f"{n}/{games} games, NNUE score {avg:.3f} "
                      f"(elo {elo(avg):+.0f})", flush=True)

    n = len(scores)
    w = scores.count(1.0)
    d = scores.count(0.5)
    l = scores.count(0.0)
    s = sum(scores) / n
    var = sum((x - s) ** 2 for x in scores) / (n - 1)
    stderr = math.sqrt(var / n)
    lo, hi = elo(s - 1.96 * stderr), elo(s + 1.96 * stderr)

    print(f"\nNNUE vs HCE: +{w} ={d} -{l}  score {s:.3f}")
    print(f"Elo diff: {elo(s):+.1f}  (95% CI {lo:+.1f} .. {hi:+.1f})")


if __name__ == "__main__":
    main()
