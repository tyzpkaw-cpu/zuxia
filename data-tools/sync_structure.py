#!/usr/bin/env python3
"""Copy 应物's structure keys into 足下, character by character.

足下 and 应物 share the first two rungs of the ladder: 全拼, then
全拼＋结构. That sharing is the reason 足下 keeps the structure key at all --
a 应物 typist's `qingzs` has to stay valid. It only holds if the two products
assign the *same* structure key to the same character, and 足下 currently
derives its key from IDS data, which disagrees with any human reading for
characters the data splits into strokes (山 → 包围, 口 → 上下, 月 → 包围).

Run this on the machine that has 应物 checked out:

    python data-tools/sync_structure.py ..\\hengma-native\\data\\hengma.dict.yaml

It writes `data-tools/sources/structure-overrides.tsv` and prints every
character where the two disagreed, so the diff is reviewable before you
regenerate. Nothing is guessed: characters 应物 does not cover are left alone.

应物's codes are 全拼 → 全拼＋结构 → 全拼＋结构＋部首, so the structure key is
the character that follows the bare pinyin. The bare pinyin is recovered as a
prefix-forest root, exactly as in audit_zuxia.py.
"""
from __future__ import annotations

import argparse
import collections
import pathlib
import sys

STRUCTURE = set("zsbpd")


def read_dict(path: pathlib.Path) -> dict[str, set[str]]:
    by_char: dict[str, set[str]] = collections.defaultdict(set)
    started = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if not started:
            started = line.startswith("...")
            continue
        if line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 2 and len(parts[0]) == 1 and parts[1].isalpha():
            by_char[parts[0]].add(parts[1])
    return by_char


def structures(by_char: dict[str, set[str]]) -> tuple[dict[str, str], list[str]]:
    """{character: structure key}, plus the characters that were ambiguous."""
    out, unclear = {}, []
    for char, codes in by_char.items():
        roots = {c for c in codes
                 if not any(c != o and c.startswith(o) for o in codes)}
        keys = set()
        for code in codes:
            owner = max((r for r in roots if code.startswith(r)),
                        key=len, default=None)
            if owner is not None and len(code) == len(owner) + 1:
                keys.add(code[-1])
        keys &= STRUCTURE
        if len(keys) == 1:
            out[char] = keys.pop()
        elif keys:
            unclear.append(char)
    return out, unclear


def main() -> int:
    here = pathlib.Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hengma_dict", type=pathlib.Path,
                    help="应物's data/hengma.dict.yaml")
    ap.add_argument("--zuxia-dict", type=pathlib.Path,
                    default=here.parent / "data/zuxia.dict.yaml")
    ap.add_argument("--out", type=pathlib.Path,
                    default=here / "sources/structure-overrides.tsv")
    ap.add_argument("--dry-run", action="store_true",
                    help="report the differences without writing the file")
    args = ap.parse_args()

    if not args.hengma_dict.exists():
        print(f"没找到 {args.hengma_dict}", file=sys.stderr)
        return 2

    theirs, unclear = structures(read_dict(args.hengma_dict))
    mine, _ = structures(read_dict(args.zuxia_dict))
    print(f"应物词典 {len(theirs)} 字取到结构码"
          f"（{len(unclear)} 字取不准，已跳过）")

    shared = sorted(set(theirs) & set(mine))
    diff = [(c, mine[c], theirs[c]) for c in shared if mine[c] != theirs[c]]
    print(f"两边都有的 {len(shared)} 字里，{len(diff)} 字判定不同"
          f"（{len(diff) / max(1, len(shared)):.1%}）")
    moves = collections.Counter(f"{a}→{b}" for _, a, b in diff)
    for move, n in moves.most_common():
        sample = "".join(c for c, a, b in diff if f"{a}→{b}" == move)[:24]
        print(f"  {move}  {n:5d}   {sample}")

    if args.dry_run:
        return 0
    lines = [
        "# 字 → 结构码。由 data-tools/sync_structure.py 从应物词典抄来，勿手改。",
        f"# 来源：{args.hengma_dict}",
        f"# 共 {len(theirs)} 字；生成时与足下自身判定有 {len(diff)} 字不同。",
        "",
    ]
    lines += [f"{c}\t{theirs[c]}" for c in sorted(theirs)]
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"已写入 {args.out}；重新跑 generate_zuxia.py 生效。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
