#!/usr/bin/env python3
"""Measure how well a generated dictionary separates characters.

A code is only worth its keystroke if it shortens the candidate list. This
reports, weighted by how often each character is actually used, how far a
typist gets at each rung of the ladder.

Two estimators appear below and they are not interchangeable -- mixing them is
how a wrong number ends up in a README:

* per (code, character) pair, weighted by that row's frequency. This is what
  「首选」 means everywhere in this project and what 应物 publishes, because it
  answers the question a typist actually asks: I pressed a code I know, is my
  character first? A character with many accepted codes therefore counts once
  per code.
* per character. Averaging each character's win rate first and only then
  weighting by frequency. It answers a different question and comes out
  several points lower at the full code. Not used here.
"""
from __future__ import annotations

import argparse
import collections
import io
import json
import pathlib
import sys
import unicodedata


def strip_tone(text: str) -> str:
    text = text.lower().replace("ü", "v").replace("u:", "v")
    return "".join(c for c in unicodedata.normalize("NFD", text)
                   if not unicodedata.combining(c))


def load_readings(path: pathlib.Path) -> dict[str, set[str]]:
    """character -> its toneless pinyin readings, from the hanzi dictionary.

    The rungs of the ladder cannot be recovered from code length alone: 行 has
    three readings of four letters each, and a two-letter reading can be the
    prefix of a three-letter one (a / ai), so the shortest code of a character
    is not always its pinyin. Reading the same source the generator reads is
    the only way to split 「+1 位」 from 「+2 位」 without guessing.
    """
    out: dict[str, set[str]] = collections.defaultdict(set)
    if not path.exists():
        return {}
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except ValueError:
                continue
            char = row.get("character")
            if not char:
                continue
            for reading in row.get("pinyin") or []:
                out[char].add(strip_tone(reading))
    return out


def pad(text: str, width: int) -> str:
    """Left-align to a terminal-column width, counting CJK as two columns."""
    used = sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1
               for ch in text)
    return text + " " * max(0, width - used)


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


def rungs(rows, by_char, readings, out):
    """The per-rung table: what each added key is actually worth."""
    if not readings:
        out.write("\n  （缺少 sources/hanzi-dictionary.txt，跳过分段统计）\n")
        return

    def level(char: str, code: str):
        own = by_char[char]
        heads = [len(p) for p in readings.get(char, ())
                 if code.startswith(p) and p in own]
        if not heads:
            # No reading data for this character: fall back to its own
            # shortest codes, which is right whenever no reading is a prefix
            # of another.
            heads = [len(c) for c in own
                     if code.startswith(c)
                     and not any(o != c and c.startswith(o) for o in own)]
        return len(code) - max(heads) if heads else None

    by_code: dict[str, list[tuple[str, int]]] = collections.defaultdict(list)
    for text, code, weight in rows:
        by_code[code].append((text, weight))
    rank: dict[tuple[str, str], int] = {}
    for code, items in by_code.items():
        for position, (text, _) in enumerate(
            sorted(items, key=lambda t: -t[1]), 1
        ):
            rank[(code, text)] = position

    # weight, first, top3, keys, candidates, codes
    tally: dict[int, list] = collections.defaultdict(
        lambda: [0, 0, 0, 0, 0, set()])
    for text, code, weight in rows:
        rung = level(text, code)
        if rung is None:
            continue
        slot = tally[rung]
        position = rank[(code, text)]
        slot[0] += weight
        slot[1] += weight if position == 1 else 0
        slot[2] += weight if position <= 3 else 0
        slot[3] += weight * len(code)
        slot[4] += weight * len(by_code[code])
        slot[5].add(code)

    if not tally:
        return
    # The deepest designed rung is the one carrying the most distinct codes;
    # anything beyond it is the handful of rows whose reading data disagrees
    # with the generator's, and it would distort the table.
    deepest = max(tally, key=lambda k: len(tally[k][5]))
    # A three-rung ladder is 全拼 + 结构 + two parts (0.2.0); a two-rung ladder
    # is the structure-less 0.1.0 layout.  Naming them by what they are beats
    # naming them by position, and it keeps a --no-structure build honest.
    if deepest >= 3:
        names = {0: "全拼", 1: "＋结构", 2: "＋部件1", 3: "＋部件2"}
    else:
        names = {0: "全拼", 1: "＋部件1", 2: "＋部件2"}

    out.write("\n  分段（按字频加权，逐「码，字」对）\n")
    out.write(f"    {pad('段', 10)}{'平均键长':>8}{'首选':>10}{'前三':>10}"
              f"{'平均候选':>10}{'不同码':>9}\n")
    first_rate: dict[int, float] = {}
    noise = 0
    for rung in sorted(tally):
        weight, first, top3, keys, cand, codes = tally[rung]
        if rung > deepest:
            noise += len(codes)
            continue
        first_rate[rung] = first / weight
        out.write(f"    {pad(names.get(rung, '+%d' % rung), 10)}{keys/weight:>8.2f}"
                  f"{first/weight:>9.2%}{top3/weight:>10.2%}"
                  f"{cand/weight:>10.2f}{len(codes):>9d}\n")
    if noise:
        out.write(f"    （尾噪：{noise} 个码的读音数据与生成器不一致，已排除）\n")

    added = sum(1 - first_rate[r] for r in range(deepest) if r in first_rate)
    pinyin_keys = tally[0][3] / tally[0][0] if tally[0][0] else 0
    out.write(f"\n    平均附加键 {added:.3f}"
              f"（＝各段未排第一的概率之和，与应物 0.685 同口径）\n")
    out.write(f"    总击键／字 {pinyin_keys + added:.2f}"
              f"    打满后仍需选字 {1 - first_rate[deepest]:.2%}\n")


def report(path: pathlib.Path, label: str, out, readings):
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

    rungs(rows, by_char, readings, out)


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):
        pass
    root = pathlib.Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dictionaries", nargs="+", type=pathlib.Path)
    ap.add_argument("--labels", nargs="*", default=None)
    ap.add_argument("--readings", type=pathlib.Path,
                    default=root / "sources/hanzi-dictionary.txt")
    args = ap.parse_args()
    labels = args.labels or [str(p) for p in args.dictionaries]
    readings = load_readings(args.readings)
    out = io.StringIO()
    for path, label in zip(args.dictionaries, labels):
        report(path, label, out, readings)
    print(out.getvalue())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
