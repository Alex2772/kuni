#!/usr/bin/env python3
"""Удаляет записи дневника со score < порога.

По умолчанию только показывает, что будет удалено (dry-run).
Для реального удаления нужен флаг --apply.

Usage: diary_prune.py [DIR] [--field score|usageCount] [--threshold 1.0] [--apply]
"""
import argparse
import re
from pathlib import Path

DEFAULT_DIR = "/var/home/alex2772/CLionProjects/kuni/cmake-build-debug/bin/data/diary"
NUM = r'(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)'


def read_field(path: Path, field: str):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        head = f.read(512)
    m = re.search(rf'"{field}"\s*:\s*{NUM}', head.replace("\n", "").replace("\r", ""))
    return float(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", nargs="?", default=DEFAULT_DIR)
    ap.add_argument("--field", choices=["score", "usageCount"], default="usageCount",
                    help="по какому полю отбирать")
    ap.add_argument("--threshold", type=float, default=1.0, help="удалять записи с field < threshold")
    ap.add_argument("--apply", action="store_true", help="реально удалить файлы (иначе dry-run)")
    args = ap.parse_args()

    total = skipped = 0
    to_delete = []
    for p in Path(args.dir).glob("*.md"):
        total += 1
        s = read_field(p, args.field)
        if s is None:
            skipped += 1  # не удаляем файлы, у которых не удалось прочитать поле
        elif s < args.threshold:
            to_delete.append(p)

    print(f"total: {total}, unparsed (kept): {skipped}, {args.field} < {args.threshold}: {len(to_delete)}")
    if not args.apply:
        print("dry-run: ничего не удалено, добавьте --apply")
        return

    for p in to_delete:
        p.unlink()
    print(f"deleted {len(to_delete)} files")


if __name__ == "__main__":
    main()
