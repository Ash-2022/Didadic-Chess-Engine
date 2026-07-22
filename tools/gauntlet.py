"""Multi-engine SOTA gauntlet: our engine vs a roster of UCI engines.

Generalizes calibrate.py from Stockfish-only anchors to any set of opponent
engines with known reference ratings (e.g., CCRL Blitz). Plays color-swapped
game pairs from shared random openings, reports per-opponent W/D/L, score,
and implied Elo difference, then pools all games into a maximum-likelihood
rating estimate with a profile-likelihood 95% confidence interval.

Config is a JSON file:

    {
      "movetime_ms": 150,
      "games_per_opponent": 40,
      "opponents": [
        {"name": "Stockfish UCI_Elo 2300",
         "path": "C:/engines/stockfish.exe",
         "rating": 2300,
         "options": {"UCI_LimitStrength": true, "UCI_Elo": 2300, "Hash": 64}},
        {"name": "SomeEngine 1.0",
         "path": "C:/engines/someengine.exe",
         "rating": 2450,
         "options": {"Hash": 64}},
        {"name": "Lc0 800 nodes",
         "path": "C:/engines/lc0.exe",
         "rating": null,
         "nodes": 800,
         "options": {"Hash": 64}}
      ]
    }

"rating" is the opponent's reference rating (from a published list at a
comparable time control); the pooled estimate is only as meaningful as
those anchors, so record the source of each rating alongside the config.

Engines without UCI_LimitStrength/UCI_Elo (checked at startup) can be
handicapped with a per-opponent "nodes" limit instead. A node-limited
engine has no published rating, so set "rating" to null: its games are
reported (score, implied Elo difference) but excluded from the pooled
absolute estimate.

Usage:
    python tools/gauntlet.py <our.exe> <config.json> [workers]
"""
import json
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
    our_exe, opp, opening, we_are_white, movetime = args
    our_limit = chess.engine.Limit(time=movetime)
    # opponents without UCI_Elo support can be handicapped by fixed nodes
    if "nodes" in opp:
        their_limit = chess.engine.Limit(nodes=opp["nodes"])
    else:
        their_limit = our_limit
    engines = {}
    try:
        ours = chess.engine.SimpleEngine.popen_uci(our_exe)
        ours.configure({"Hash": 128, "Threads": 1})
        engines["us"] = ours

        them = chess.engine.SimpleEngine.popen_uci(opp["path"])
        opts = dict(opp.get("options", {}))
        opts.setdefault("Threads", 1)
        try:
            them.configure(opts)
        except chess.engine.EngineError:
            # engine may not expose every option (e.g., Threads); retry without
            them.configure(opp.get("options", {}))
        engines["them"] = them

        board = chess.Board()
        for uci in opening:
            board.push_uci(uci)

        white = "us" if we_are_white else "them"
        black = "them" if we_are_white else "us"

        while not board.is_game_over(claim_draw=True) and board.ply() < MAX_PLIES:
            side = white if board.turn == chess.WHITE else black
            limit = our_limit if side == "us" else their_limit
            result = engines[side].play(board, limit)
            if result.move is None:
                raise RuntimeError(f"{side} returned no move")
            board.push(result.move)

        outcome = board.outcome(claim_draw=True)
        if outcome is None or outcome.winner is None:
            return (opp["name"], 0.5)
        winner = white if outcome.winner == chess.WHITE else black
        return (opp["name"], 1.0 if winner == "us" else 0.0)
    finally:
        for eng in engines.values():
            try:
                eng.quit()
            except Exception:
                pass


def expected(delta):
    return 1.0 / (1.0 + 10.0 ** (-delta / 400.0))


def elo(s):
    s = min(max(s, 1e-6), 1 - 1e-6)
    return -400.0 * math.log10(1.0 / s - 1.0)


def nll(rating, games):
    """Negative log-likelihood of our rating over (opponent_rating, score) games."""
    total = 0.0
    for opp_rating, s in games:
        p = min(max(expected(rating - opp_rating), 1e-9), 1 - 1e-9)
        total -= s * math.log(p) + (1.0 - s) * math.log(1.0 - p)
    return total


def fit_elo_ci(games):
    """MLE rating plus profile-likelihood 95% CI (delta-NLL <= 1.9207)."""
    best_r, best_nll = None, float("inf")
    for r10 in range(12000, 36000):
        r = r10 / 10.0
        v = nll(r, games)
        if v < best_nll:
            best_nll, best_r = v, r
    lo = best_r
    while lo > 1200.0 and nll(lo, games) < best_nll + 1.9207:
        lo -= 0.1
    hi = best_r
    while hi < 3600.0 and nll(hi, games) < best_nll + 1.9207:
        hi += 0.1
    return best_r, lo, hi


def main():
    our_exe, cfg_path = sys.argv[1], sys.argv[2]
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 8

    with open(cfg_path) as f:
        cfg = json.load(f)
    movetime = cfg.get("movetime_ms", 150) / 1000.0
    per_opp = cfg.get("games_per_opponent", 40)
    per_opp -= per_opp % 2
    opponents = cfg["opponents"]

    # Fail fast: a config option the engine doesn't expose would otherwise
    # error mid-run, or worse, silently play at full strength.
    for opp in opponents:
        eng = chess.engine.SimpleEngine.popen_uci(opp["path"])
        missing = [k for k in opp.get("options", {}) if k not in eng.options]
        eng.quit()
        if missing:
            sys.exit(f"config error: {opp['name']} does not support "
                     f"option(s) {missing}. Engines without UCI_Elo can be "
                     f"handicapped with a per-opponent \"nodes\" limit "
                     f"(set \"rating\" to null if the anchor is unknown).")

    tasks = []
    for oi, opp in enumerate(opponents):
        for pair in range(per_opp // 2):
            opening = random_opening(9000 + oi * 3499 + pair)
            tasks.append((our_exe, opp, opening, True, movetime))
            tasks.append((our_exe, opp, opening, False, movetime))

    results = []
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for i, r in enumerate(pool.map(play_game, tasks)):
            results.append(r)
            if (i + 1) % 20 == 0:
                print(f"{i + 1}/{len(tasks)} games", flush=True)

    games = []  # (opponent_rating, score) per game, for pooled MLE
    rows = []
    for opp in opponents:
        scores = [s for (name, s) in results if name == opp["name"]]
        if not scores:
            continue
        rating = opp.get("rating")
        if rating is not None:
            games.extend((rating, s) for s in scores)
        n = len(scores)
        w = scores.count(1.0)
        d = scores.count(0.5)
        l = scores.count(0.0)
        avg = sum(scores) / n
        margin = 1.96 * math.sqrt(avg * (1.0 - avg) / n)  # 95% binomial
        rows.append((opp, rating, n, w, d, l, avg, margin))

    est = lo = hi = None
    if games:
        est, lo, hi = fit_elo_ci(games)

    def rated_cols(rating, avg):
        """implied ΔR always; implied rating and model only with an anchor."""
        diff = elo(avg)
        if rating is None or est is None:
            return diff, None, None
        return diff, rating + diff, expected(est - rating)

    print()
    print(f"{'Opponent':<32} {'R_k':>5} {'+W':>4} {'=D':>4} {'-L':>4} "
          f"{'score':>7} {'+-1.96SE':>9} {'implDR':>7} {'implR':>6} {'model':>7}")
    for opp, rating, n, w, d, l, avg, margin in rows:
        diff, impl_r, model = rated_cols(rating, avg)
        print(f"{opp['name']:<32} {rating if rating is not None else '-':>5} "
              f"{w:>4} {d:>4} {l:>4} {avg:>6.1%} {margin:>8.1%} {diff:>+7.0f} "
              f"{f'{impl_r:.0f}' if impl_r is not None else '-':>6} "
              f"{f'{model:.1%}' if model is not None else '-':>7}")

    if est is not None:
        print(f"\nPooled MLE rating ({len(games)} games with anchors): "
              f"~{est:.0f}  (95% profile-likelihood CI [{lo:.0f}, {hi:.0f}])")
        print("Note: estimate is on the scale of the opponents' reference "
              "ratings at THIS time control; cite the rating source when "
              "reporting.")

    # Ready-to-paste LaTeX rows, same format as the report's tab:calibration.
    print("\n% --- LaTeX table body ---")
    print("% Opponent & $R_k$ & Score $s_k$ & $\\pm$1.96 SE & "
          "Implied $\\Delta R$ & Implied rating & Model $E(\\hat{R}{-}R_k)$ \\\\")
    for opp, rating, n, w, d, l, avg, margin in rows:
        diff, impl_r, model = rated_cols(rating, avg)
        print(f"{opp['name']} & {rating if rating is not None else '--'} & "
              f"{avg * 100:.1f}\\% & $\\pm${margin * 100:.1f}\\% & "
              f"${diff:+.0f}$ & "
              f"{f'{impl_r:.0f}' if impl_r is not None else '--'} & "
              f"{f'{model * 100:.1f}' if model is not None else '--'}\\% \\\\")
    if est is not None:
        print(f"\\multicolumn{{7}}{{|c|}}{{\\textbf{{Pooled MLE rating "
              f"estimate: $\\hat{{R}} \\approx {est:.0f}$ Elo \\quad "
              f"(95\\% profile-likelihood CI $\\approx$ "
              f"[{lo:.0f}, {hi:.0f}])}}}} \\\\")


if __name__ == "__main__":
    main()
