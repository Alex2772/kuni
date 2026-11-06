#!/usr/bin/env python3
"""Строит распределение score по md-файлам дневника.

Usage: diary_score_hist.py [DIR] [--bins N] [--out FILE]
"""
import argparse
import json
import re
from pathlib import Path

import matplotlib.pyplot as plt

DEFAULT_DIR = "/var/home/alex2772/CLionProjects/kuni/cmake-build-debug/bin/data/diary"
SCORE_RE = re.compile(r'"score"\s*:\s*(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)')


def read_score(path: Path):
    # JSON в начале файла (после "---"); embedding длинный, а score идёт первым,
    # поэтому читаем только начало файла.
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        head = f.read(512)
    head = head.replace("\n", "").replace("\r", "")  # на случай переносов внутри JSON
    m = SCORE_RE.search(head)
    return float(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", nargs="?", default=DEFAULT_DIR)
    ap.add_argument("--bins", type=int, default=100)
    ap.add_argument("--out", default="score_hist.png")
    args = ap.parse_args()

    scores, bad = [], 0
    for p in Path(args.dir).glob("*.md"):
        s = read_score(p)
        if s is None:
            bad += 1
        else:
            scores.append(s)

    print(f"files with score: {len(scores)}, unparsed: {bad}")
    if not scores:
        return
    print(f"min={min(scores):.4f} max={max(scores):.4f} mean={sum(scores)/len(scores):.4f}")

    scores.sort()
    p99 = scores[int(len(scores) * 0.99)]
    print(f"p50={scores[len(scores)//2]:.4f} p90={scores[int(len(scores)*0.9)]:.4f} p99={p99:.4f}")

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    axes[0].hist(scores, bins=args.bins, log=True)
    axes[0].set_title(f"Весь диапазон, лог. шкала Y ({len(scores)} файлов)")
    axes[1].hist(scores, bins=args.bins, range=(0, p99))
    axes[1].set_title(f"До 99-го перцентиля ({p99:.2f})")
    for ax in axes:
        ax.set_xlabel("score")
        ax.set_ylabel("количество")
        ax.grid(alpha=0.3)
    fig.tight_layout()
    plt.savefig(args.out, dpi=120)
    print(f"saved to {args.out}")
    plt.show()


if __name__ == "__main__":
    main()
