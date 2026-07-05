"""NNUE trainer for the (768 -> H)x2 -> 1 SCReLU perspective network.

Consumes datagen output lines:  <fen> | <white-POV score cp> | <white-POV result>
Trains with sigmoid-MSE loss on a blend of search score and game result,
then quantizes (QA=255, QB=64) and writes nnue_weights.bin in the engine's
format (see Engine_Core/src/nnue.h).

Usage:
    python tools/train_nnue.py "data/dg_*.txt" nnue_weights.bin [epochs] [hidden]
"""
import glob
import struct
import sys
import time

import numpy as np
import scipy.sparse as sp

INPUT = 768
HIDDEN = 128  # overridable via argv[4]; must match nnue.h HIDDEN_SIZE
SCALE, QA, QB = 400, 255, 64
LAMBDA = 0.75          # weight of search score vs game result in the target
LR = 1e-3
BATCH = 16384
VAL_FRACTION = 0.02
WEIGHT_CLIP = 1.98     # keeps int16 quantization exact and accumulators safe

# Engine Piece enum order: PAWN,ROOK,KNIGHT,BISHOP,QUEEN,KING
PIECE_IDX = {'p': 0, 'r': 1, 'n': 2, 'b': 3, 'q': 4, 'k': 5}


def load_dataset(pattern):
    files = sorted(glob.glob(pattern))
    if not files:
        sys.exit(f"no files match {pattern}")
    lines = []
    for path in files:
        with open(path, 'r') as f:
            lines.extend(f.readlines())
    n = len(lines)
    print(f"{n} positions from {len(files)} files")

    idx_w = np.full((n, 32), -1, dtype=np.int16)  # white-perspective features
    idx_b = np.full((n, 32), -1, dtype=np.int16)  # black-perspective features
    stm_white = np.zeros(n, dtype=bool)
    score = np.zeros(n, dtype=np.float32)          # white POV cp
    result = np.zeros(n, dtype=np.float32)         # white POV 1/0.5/0

    keep = 0
    for line in lines:
        parts = line.split('|')
        if len(parts) != 3:
            continue
        try:
            line_score = float(parts[1])
            line_result = float(parts[2])
        except ValueError:
            continue  # truncated/corrupt line
        fen = parts[0].strip()
        fields = fen.split()
        board, stm = fields[0], fields[1]

        k = 0
        rank = 7
        file = 0
        ok = True
        for ch in board:
            if ch == '/':
                rank -= 1
                file = 0
            elif ch.isdigit():
                file += int(ch)
            else:
                p = PIECE_IDX.get(ch.lower())
                if p is None or k >= 32:
                    ok = False
                    break
                sq = rank * 8 + file
                white_piece = ch.isupper()
                idx_w[keep, k] = (0 if white_piece else 384) + p * 64 + sq
                idx_b[keep, k] = (384 if white_piece else 0) + p * 64 + (sq ^ 56)
                k += 1
                file += 1
        if not ok or k == 0:
            continue
        stm_white[keep] = (stm == 'w')
        score[keep] = line_score
        result[keep] = line_result
        keep += 1

    print(f"parsed {keep} positions")
    return idx_w[:keep], idx_b[:keep], stm_white[:keep], score[:keep], result[:keep]


def make_csr(idx_batch):
    mask = idx_batch >= 0
    counts = mask.sum(axis=1).astype(np.int32)
    indices = idx_batch[mask].astype(np.int32)
    indptr = np.zeros(len(counts) + 1, dtype=np.int32)
    np.cumsum(counts, out=indptr[1:])
    data = np.ones(len(indices), dtype=np.float32)
    return sp.csr_matrix((data, indices, indptr), shape=(len(counts), INPUT))


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


class Adam:
    def __init__(self, shape, lr):
        self.m = np.zeros(shape, dtype=np.float32)
        self.v = np.zeros(shape, dtype=np.float32)
        self.lr = lr
        self.t = 0

    def step(self, w, g):
        self.t += 1
        self.m = 0.9 * self.m + 0.1 * g
        self.v = 0.999 * self.v + 0.001 * (g * g)
        mh = self.m / (1 - 0.9 ** self.t)
        vh = self.v / (1 - 0.999 ** self.t)
        w -= self.lr * mh / (np.sqrt(vh) + 1e-8)


def main():
    global HIDDEN
    pattern = sys.argv[1] if len(sys.argv) > 1 else "dgdata/dg_*.txt"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "nnue_weights.bin"
    epochs = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    if len(sys.argv) > 4:
        HIDDEN = int(sys.argv[4])

    idx_w, idx_b, stm_white, score, result = load_dataset(pattern)
    n = len(score)

    # Side-to-move perspective: features, score, result
    idx_stm = np.where(stm_white[:, None], idx_w, idx_b)
    idx_ntm = np.where(stm_white[:, None], idx_b, idx_w)
    del idx_w, idx_b
    score_stm = np.where(stm_white, score, -score)
    result_stm = np.where(stm_white, result, 1.0 - result)
    target = (LAMBDA * sigmoid(score_stm / SCALE)
              + (1.0 - LAMBDA) * result_stm).astype(np.float32)

    # Train/validation split
    rng = np.random.default_rng(1234)
    perm = rng.permutation(n)
    n_val = max(1, int(n * VAL_FRACTION))
    val_rows, train_rows = perm[:n_val], perm[n_val:]
    n_train = len(train_rows)
    print(f"train {n_train}, val {n_val}")

    # Parameters
    w1 = (rng.standard_normal((INPUT, HIDDEN)) * 0.05).astype(np.float32)
    b1 = np.zeros(HIDDEN, dtype=np.float32)
    w2 = (rng.standard_normal(2 * HIDDEN) * 0.05).astype(np.float32)
    b2 = np.zeros(1, dtype=np.float32)
    opt_w1, opt_b1 = Adam(w1.shape, LR), Adam(b1.shape, LR)
    opt_w2, opt_b2 = Adam(w2.shape, LR), Adam(b2.shape, LR)

    def forward(rows):
        xs = make_csr(idx_stm[rows])
        xn = make_csr(idx_ntm[rows])
        acc_s = xs @ w1 + b1
        acc_n = xn @ w1 + b1
        # SCReLU: clamp(x,0,1)^2 — must match the engine's dotHalf()
        cs = np.clip(acc_s, 0.0, 1.0)
        cn = np.clip(acc_n, 0.0, 1.0)
        hs = cs * cs
        hn = cn * cn
        out = hs @ w2[:HIDDEN] + hn @ w2[HIDDEN:] + b2[0]
        return xs, xn, acc_s, acc_n, cs, cn, hs, hn, out

    def val_loss():
        total = 0.0
        for i in range(0, n_val, BATCH):
            rows = val_rows[i:i + BATCH]
            out = forward(rows)[-1]
            p = sigmoid(out)
            total += float(np.sum((p - target[rows]) ** 2))
        return total / n_val

    print(f"initial val loss {val_loss():.6f}")

    for epoch in range(epochs):
        # Step LR decay: drop at 60% and 85% of the schedule
        decay = 1.0
        if epoch >= int(epochs * 0.85):
            decay = 0.1
        elif epoch >= int(epochs * 0.6):
            decay = 0.3
        for opt in (opt_w1, opt_b1, opt_w2, opt_b2):
            opt.lr = LR * decay

        t0 = time.time()
        rng.shuffle(train_rows)
        total = 0.0
        for i in range(0, n_train - BATCH + 1, BATCH):
            rows = train_rows[i:i + BATCH]
            xs, xn, acc_s, acc_n, cs, cn, hs, hn, out = forward(rows)
            p = sigmoid(out)
            err = p - target[rows]
            total += float(np.sum(err ** 2))

            dout = (2.0 / len(rows)) * err * p * (1.0 - p)
            g_w2 = np.concatenate([hs.T @ dout, hn.T @ dout])
            g_b2 = np.array([dout.sum()], dtype=np.float32)

            # d(SCReLU)/dx = 2*clamp(x,0,1) inside (0,1), 0 outside
            dhs = np.outer(dout, w2[:HIDDEN])
            dhn = np.outer(dout, w2[HIDDEN:])
            dacc_s = dhs * 2.0 * cs * ((acc_s > 0) & (acc_s < 1))
            dacc_n = dhn * 2.0 * cn * ((acc_n > 0) & (acc_n < 1))

            g_w1 = (xs.T @ dacc_s) + (xn.T @ dacc_n)
            g_w1 = np.asarray(g_w1, dtype=np.float32)
            g_b1 = dacc_s.sum(axis=0) + dacc_n.sum(axis=0)

            opt_w1.step(w1, g_w1)
            opt_b1.step(b1, g_b1)
            opt_w2.step(w2, g_w2)
            opt_b2.step(b2, g_b2)

            np.clip(w1, -WEIGHT_CLIP, WEIGHT_CLIP, out=w1)
            np.clip(b1, -WEIGHT_CLIP, WEIGHT_CLIP, out=b1)
            np.clip(w2, -WEIGHT_CLIP, WEIGHT_CLIP, out=w2)

        n_batches = (n_train // BATCH) * BATCH
        print(f"epoch {epoch + 1}/{epochs}: train {total / n_batches:.6f} "
              f"val {val_loss():.6f} ({time.time() - t0:.1f}s)")

    # Quantize and write (file version 2 = SCReLU)
    w1q = np.round(w1 * QA).astype(np.int16)
    b1q = np.round(b1 * QA).astype(np.int16)
    w2q = np.round(w2 * QB).astype(np.int16)
    b2q = int(round(float(b2[0]) * QA * QA * QB))

    with open(out_path, "wb") as f:
        f.write(b"NNUE")
        f.write(struct.pack("<III", 2, INPUT, HIDDEN))
        f.write(w1q.tobytes())   # [feature][hidden], row-major
        f.write(b1q.tobytes())
        f.write(w2q.tobytes())
        f.write(struct.pack("<i", b2q))
    print(f"wrote {out_path}")

    # Sanity: report eval spread on validation set
    rows = val_rows[:4096]
    out = forward(rows)[-1]
    cp = out * SCALE
    corr = np.corrcoef(cp, score_stm[rows])[0, 1]
    print(f"val eval-vs-search-score correlation: {corr:.4f}")


if __name__ == "__main__":
    main()
