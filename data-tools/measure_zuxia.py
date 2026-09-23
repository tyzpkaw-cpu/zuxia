#!/usr/bin/env python3
"""Measure how well a generated dictionary separates characters.

A code is only worth its keystroke if it shortens the candidate list. This
reports, weighted by how often each character is actually used, how far a
typist gets at each rung of the ladder.
"""
from __future__ import annotations

import argparse
import collections
import io
import pathlib
import sys


def entries(path: pathlib.Path):
    started = False
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if not started:
                if line.startswith("..."):
                    started = True
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 3:
                yield parts[0], parts[1], int(parts[2])


def report(path: pathlib.Path, label: str, out):
    rows = list(entries(path))
    by_code: dict[str, list[tuple[str, int]]] = collections.defaultdict(list)
    by_char: dict[str, set[str]] = collections.defaultdict(set)
    for text, code, weight in rows:
        by_code[code].append((text, weight))
        by_char[text].add(code)

    stem = collections.defaultdict(lambda: collections.defaultdict(list))
    for code, items in by_code.items():
        stem[len(code)][code] = items

    out.write(f"\n{'=' * 70}\n{label}\n{'=' * 70}\n")
    out.write(f"  字 {len(by_char)}，码 {len(by_code)}，行 {len(rows)}\n")
    out.write(f"  平均每字 {len(rows)/len(by_char):.2f} 个码\n")

    # Weighted: for each (code, character) pair, where does the character land?
    total = first = top3 = size = 0
    for code, items in by_code.items():
        ordered = sorted(items, key=lambda t: -t[1])
        for rank, (_, weight) in enumerate(ordered, 1):
            total += weight
            size += weight * len(items)
            if rank == 1:
                first += weight
            if rank <= 3:
                top3 += weight
    out.write(f"\n  全部码合计（按字频加权）\n")
    out.write(f"    首选命中 {first/total:6.2%}   前三 {top3/total:6.2%}   "
              f"平均同码候选 {size/total:5.2f}\n")

    # The deepest code each character owns is what a determined typist reaches.
    deepest = collections.defaultdict(list)
    for char, codes in by_char.items():
        longest = max(len(c) for c in codes)
        for c in codes:
            if len(c) == longest:
                deepest[c].append(char)
    unique = sum(1 for c, chars in deepest.items() if len(chars) == 1)
    out.write(f"    打满时唯一的码占比 {unique/len(deepest):6.2%}\n")

    out.write(f"\n  分码长\n")
    for length in sorted(stem):
        codes = stem[length]
        t = f = s = 0
        for items in codes.values():
            for rank, (_, w) in enumerate(sorted(items, key=lambda x: -x[1]), 1):
                t += w
                s += w * len(items)
                if rank == 1:
                    f += w
        if t:
            out.write(f"    {length:2d} 键  码数 {len(codes):6d}  "
                      f"首选 {f/t:6.2%}  平均候选 {s/t:6.2f}\n")


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):
        pass
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dictionaries", nargs="+", type=pathlib.Path)
    ap.add_argument("--labels", nargs="*", default=None)
    args = ap.parse_args()
    labels = args.labels or [str(p) for p in args.dictionaries]
    out = io.StringIO()
    for path, label in zip(args.dictionaries, labels):
        report(path, label, out)
    print(out.getvalue())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
