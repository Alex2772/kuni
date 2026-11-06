#!/usr/bin/env python3
"""Строит распределения usageCount и lastUsed по md-файлам дневника.

Usage: diary_usage_hist.py [DIR] [--bins N] [--out FILE]
"""
import argparse
import re
from datetime import datetime
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt

DEFAULT_DIR = "/var/home/alex2772/CLionProjects/kuni/cmake-build-debug/bin/data/diary"
USAGE_RE = re.compile(r'"usageCount"\s*:\s*(\d+)')
LAST_USED_RE = re.compile(r'"lastUsed"\s*:\s*"([^"]+)"')


def read_header(path: Path) -> str:
    # embedding длинный и идёт после метаданных, поэтому читаем только начало
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        head = f.read(512)
    return head.replace("\n", "").replace("\r", "")


def parse_time(s: str):
    # "2026-09-18 17:15:51.314862412" -> обрезаем до микросекунд
    m = re.match(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})(?:\.(\d+))?", s)
    if not m:
        return None
    base, frac = m.groups()
    return datetime.strptime(f"{base}.{(frac or '0')[:6].ljust(6, '0')}", "%Y-%m-%d %H:%M:%S.%f")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", nargs="?", default=DEFAULT_DIR)
    ap.add_argument("--bins", type=int, default=50)
    ap.add_argument("--out", default="diary_usage_hist.png")
    args = ap.parse_args()

    usages, times = [], []
    total = 0
    for p in Path(args.dir).glob("*.md"):
        total += 1
        head = read_header(p)
        m = USAGE_RE.search(head)
        if m:
            usages.append(int(m.group(1)))
        m = LAST_USED_RE.search(head)
        if m:
            t = parse_time(m.group(1))
            if t:
                times.append(t)

    print(f"files: {total}, usageCount parsed: {len(usages)}, lastUsed parsed: {len(times)}")
    if usages:
        usages.sort()
        print(f"usageCount: min={usages[0]} max={usages[-1]} mean={sum(usages)/len(usages):.2f} "
              f"p50={usages[len(usages)//2]} p90={usages[int(len(usages)*0.9)]}")
    if times:
        print(f"lastUsed: min={min(times)} max={max(times)}")

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    if usages:
        axes[0].hist(usages, bins=range(0, usages[-1] + 2), log=True, align="left")
    axes[0].set_title(f"usageCount, лог. шкала Y ({len(usages)} файлов)")
    axes[0].set_xlabel("usageCount")
    axes[0].set_ylabel("количество")

    if times:
        axes[1].hist(mdates.date2num(times), bins=args.bins)
        axes[1].xaxis_date()
        fig.autofmt_xdate()
    axes[1].set_title(f"lastUsed ({len(times)} файлов)")
    axes[1].set_xlabel("время")
    axes[1].set_ylabel("количество")
    for ax in axes:
        ax.grid(alpha=0.3)
    fig.tight_layout()
    plt.savefig(args.out, dpi=120)
    print(f"saved to {args.out}")
    plt.show()


if __name__ == "__main__":
    main()
