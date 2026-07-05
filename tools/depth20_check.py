"""Depth-20 validation: runs `go depth 20` on a set of positions, times each
search, and legality-checks every PV move and bestmove with python-chess.

Usage:
    python tools/depth20_check.py <engine.exe> [--nonnue] [depth]
"""
import subprocess
import sys
import time

import chess

FENS = [
    ("startpos", chess.STARTING_FEN),
    ("bench pos", "r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2"),
    ("kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
    ("endgame", "8/2k5/3p4/p2P1p2/P2P1P2/8/8/4K3 w - - 0 1"),
    ("middlegame", "r2q1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 9"),
]


def main():
    exe = sys.argv[1]
    extra = [a for a in sys.argv[2:] if a.startswith("--")]
    depth = next((int(a) for a in sys.argv[2:] if a.isdigit()), 20)

    proc = subprocess.Popen(
        [exe] + extra, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1)

    def send(cmd):
        proc.stdin.write(cmd + "\n")
        proc.stdin.flush()

    def wait_for(token):
        lines = []
        while True:
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError("engine died")
            lines.append(line.strip())
            if line.startswith(token):
                return lines

    send("uci")
    wait_for("uciok")
    send("setoption name Hash value 1024")
    send("setoption name Threads value 8")
    send("isready")
    wait_for("readyok")

    all_ok = True
    for name, fen in FENS:
        board = chess.Board(fen)
        send("position fen " + fen)
        send(f"go depth {depth}")
        t0 = time.time()
        lines = wait_for("bestmove")
        elapsed = time.time() - t0

        pv_moves_checked = 0
        last_info = ""
        for line in lines:
            if line.startswith("info") and " pv " in line:
                last_info = line
                pv = line.split(" pv ", 1)[1].split()
                b = chess.Board(fen)
                for uci in pv:
                    move = chess.Move.from_uci(uci)
                    if move not in b.legal_moves:
                        print(f"  ILLEGAL PV MOVE {uci} in: {line}")
                        all_ok = False
                        break
                    b.push(move)
                    pv_moves_checked += 1

        best = lines[-1].split()[1]
        best_move = chess.Move.from_uci(best)
        legal = best_move in board.legal_moves
        if not legal:
            all_ok = False

        nodes = 0
        if " nodes " in last_info:
            nodes = int(last_info.split(" nodes ")[1].split()[0])
        nps = int(nodes / elapsed) if elapsed > 0 else 0

        print(f"{name:12s} depth {depth}: {elapsed:6.2f}s  "
              f"bestmove {best} ({'legal' if legal else 'ILLEGAL'})  "
              f"{pv_moves_checked} PV moves checked  "
              f"nodes {nodes}  nps {nps}")
        print(f"  final: {last_info}")

    send("quit")
    proc.wait(timeout=10)
    print("\nRESULT:", "ALL LEGAL, ALL PASSED" if all_ok else "FAILURES FOUND")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
