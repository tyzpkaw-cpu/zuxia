#!/usr/bin/env python3
"""Generate the 足下 single-character dictionary.

足下 encodes a character as sound plus form, where the form is carried by the
character's own parts rather than by its radical alone:

    full pinyin  →  + structure  →  + one component letter  →  + two

The structure key is one of `z` 左右 / `s` 上下 / `b` 包围 / `p` 品字形及兜底
/ `d` 独体, derived mechanically from the character's IDS. It is weak on its
own (1.34 bit) but it is orthogonal to the component keys and it costs no
knowledge: one look tells you whether a character is side-by-side or stacked.

A component's letter is the initial of what people **call** it, so 氵 is s
(水) and 宀 is b (宝盖). A component with more than one accepted name yields
more than one letter, and any two distinct components may be used in either
order. All of that widens what the typist may press, which is the point: a
code nobody can guess is a code nobody can type.

Every stage is a valid code, so a typist can stop as soon as the candidate
they want is in reach.
"""
from __future__ import annotations

import argparse
import sys
import collections
import json
import pathlib
import unicodedata

BINARY_IDS = set("⿰⿱⿴⿵⿶⿷⿸⿹⿺⿻")
TERNARY_IDS = set("⿲⿳")
ENCLOSURE_IDS = set("⿴⿵⿶⿷⿸⿹⿺")

# Markers and stray latin letters leak out of the IDS sources; they are not
# components and must never become part of a code.
NOT_A_COMPONENT = set("？?[]{}()0123456789"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "abcdefghijklmnopqrstuvwxyz")

# 独体字 are the one class the IDS data cannot be trusted on: it decomposes
# them into strokes rather than components, and the stroke tree is arbitrary --
# it makes 山 look enclosed (⿶凵丨) and 口 look stacked (⿱丨乛一).  Checked
# once against the national standard: of its 256 characters the generator got
# 42 right and 214 wrong, and those 214 are 19% of everything a typist types.
#
# So the list is read from GF 0013-2009 rather than guessed.  That standard is
# also the right authority for a different reason: its stated scope is 识字教育,
# which is exactly where a typist learned to tell 独体 from 合体 in the first
# place.  A structure key the typist cannot guess is worth nothing.
SINGLE_PATH = pathlib.Path(__file__).resolve().parent / "sources/gf0013-duti.txt"

# 〇 and 卍 are not in the standard because they are not really 汉字; 孓 is too
# rare for it. All three decompose to nothing usable, so they are named here.
EXTRA_SINGLE = set("〇卍孓")


def load_single_characters(path: pathlib.Path = SINGLE_PATH) -> set[str]:
    """The 独体字 table, straight from GF 0013-2009."""
    out: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        out |= {c for c in line if "\u4e00" <= c <= "\u9fff"}
    if len(out) != 256:
        raise SystemExit(f"{path} 应当正好 256 字，读到 {len(out)}")
    return out | EXTRA_SINGLE


SINGLE_STRUCTURE_OVERRIDES = load_single_characters()

# hanzi-dictionary.txt describes a handful of characters as one part *inserted
# into* another -- 街 is ⿻行圭, not the ⿲彳圭亍 the IDS data gives.  ⿻ means
# "overlaid", which is not one of the four shape buckets, so every one of them
# landed in the catch-all: 衡 came out hengp.  That is both unguessable and
# inconsistent, because 衎 and 衞 have no hanzi-dictionary row at all, fall back
# to the IDS tree, and so came out 左右 like a typist expects.
#
# The hosts are named rather than inferred.  Only these three are real infixes
# in the 8105 charset; every other ⿻ in that file is two strokes crossing --
# 水 is ⿻亅？, 火 is ⿻丷人 -- where trusting either tree would be worse than
# the catch-all.  ⿴ is listed too because 衝 is written ⿴行重.
INFIX_HOSTS = {"行": "z", "雔": "z", "衣": "s"}


def strip_tone(text: str) -> str:
    text = text.lower().replace("ü", "v").replace("u:", "v")
    decomposed = unicodedata.normalize("NFD", text)
    return "".join(c for c in decomposed if not unicodedata.combining(c))


def load_hanzi(path: pathlib.Path) -> dict[str, dict]:
    rows: dict[str, dict] = {}
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                row = json.loads(line)
                if row.get("character"):
                    rows[row["character"]] = row
    return rows


def load_ids(path: pathlib.Path) -> dict[str, str]:
    out: dict[str, str] = {}
    with path.open(encoding="utf-8") as handle:
        for raw in handle:
            if raw.startswith("#"):
                continue
            parts = raw.rstrip("\n").split("\t")
            if len(parts) >= 3 and parts[1] not in out:
                out[parts[1]] = parts[2]
    return out


def load_charset(path: pathlib.Path) -> list[tuple[str, str, int]]:
    out: list[tuple[str, str, int]] = []
    started = False
    skipped = 0
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if not started:
                if line.startswith("..."):
                    started = True
                continue
            # rime-ice keeps disabled rows in the body as comments, and a few
            # live rows carry prose instead of a reading ("ng  没启用",
            # "jing / dan"). Both used to sail straight through: the comment
            # marker became part of the candidate text, so `nei` offered a
            # candidate literally rendered「# 那」at weight 9,929,703.
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2 and parts[0] and parts[1]:
                reading = strip_tone(parts[1])
                if not reading.isascii() or not reading.isalpha():
                    skipped += 1
                    continue
                weight = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 1
                out.append((parts[0], reading, weight))
    if skipped:
        print(f"skipped {skipped} rows whose reading is not a pinyin syllable",
              file=sys.stderr)
    return out


def load_names(path: pathlib.Path) -> dict[str, list[str]]:
    """Minimal reader for the curated names file: `部件: [名称, 名称]`."""
    names: dict[str, list[str]] = {}
    with path.open(encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if not line or ":" not in line:
                continue
            key, value = line.split(":", 1)
            key, value = key.strip(), value.strip()
            if not key or not value.startswith("["):
                continue
            items = [v.strip() for v in value.strip("[]").split(",")]
            names[key] = [v for v in items if v]
    return names


def parse_ids(text: str):
    if not text or text == "？":
        return None

    def at(i: int):
        if i >= len(text):
            raise ValueError
        token = text[i]
        if token in BINARY_IDS:
            left, j = at(i + 1)
            right, k = at(j)
            return (token, (left, right)), k
        if token in TERNARY_IDS:
            a, j = at(i + 1)
            b, k = at(j)
            c, m = at(k)
            return (token, (a, b, c)), m
        return (token, ()), i + 1

    try:
        node, _ = at(0)
        return node
    except (ValueError, IndexError):
        return None


def leaves_of(node) -> list[str]:
    """Every terminal component of a parsed IDS tree, in reading order."""
    if node is None:
        return []
    op, children = node
    if not children:
        return [op]
    out: list[str] = []
    for child in children:
        out.extend(leaves_of(child))
    return out


def load_structure_overrides(path: pathlib.Path) -> dict[str, str]:
    """An authoritative 字→结构码 map, one `字<TAB>码` per line.

    Its reason to exist: the structure key is only worth having if the typist
    can guess it, and for a character the IDS data splits into strokes the
    derived key is a coin toss (山 comes out 包围, 口 comes out 上下). It is
    also the one place where 足下 and 应物 must agree character for character,
    or the shared rungs stop being shared.

    `data-tools/sync_structure.py` fills this file from 应物's own dictionary.
    Absent or empty, the generator falls back to the derived key.
    """
    out: dict[str, str] = {}
    if not path or not path.exists():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) == 2 and len(parts[0]) == 1 and parts[1] in set("zsbpd"):
            out[parts[0]] = parts[1]
    return out


def classify_structure(node, character: str,
                       overrides: dict[str, str] | None = None) -> str:
    if overrides and character in overrides:
        return overrides[character]
    if character in SINGLE_STRUCTURE_OVERRIDES:
        return "d"
    if node is None:
        # No decomposition at all. "Unknown" is not the same claim as 独体,
        # so it joins the catch-all rather than being called single-component.
        return "p"
    op, children = node
    if not children:
        return "d"
    # 三叠式 (品 森 晶 众 磊) reads as a shape of its own, not as 上下: the
    # typist sees three of the same part, not a stack. 应物 puts it in the
    # catch-all and 足下 must agree, or every such character would take a
    # different structure key in the two products and the codes a typist
    # already knows would stop working.
    leaves = leaves_of(node)
    if len(leaves) == 3 and len(set(leaves)) == 1:
        return "p"
    # Before the enclosure test: 衝 is ⿴行重, which is an infix, not 包围.
    if op in {"⿻", "⿴"} and children[0][0] in INFIX_HOSTS:
        return INFIX_HOSTS[children[0][0]]
    if op in {"⿰", "⿲"}:
        return "z"
    if op in {"⿱", "⿳"}:
        return "s"
    if op in ENCLOSURE_IDS:
        return "b"
    return "p"


def components_of(char: str, hanzi: dict, ids: dict) -> list[str]:
    row = hanzi.get(char) or {}
    text = row.get("decomposition") or ""
    if not text or text == "？":
        text = ids.get(char, "")
    seen: list[str] = []
    for part in text:
        if part in BINARY_IDS or part in TERNARY_IDS:
            continue
        if part in NOT_A_COMPONENT or part == char:
            continue
        if part not in seen:
            seen.append(part)
    return seen


def letters_for(part: str, names: dict, hanzi: dict) -> set[str]:
    """Every letter this component may be typed as."""
    out: set[str] = set()
    for name in names.get(part, [part]):
        head = name[0]
        readings = [strip_tone(p) for p in (hanzi.get(head) or {}).get("pinyin", [])]
        for reading in readings:
            if reading:
                out.add(reading[0])
    return {c for c in out if c.isalpha()}


def codes_for(pinyin, structure, parts, names, hanzi, use_structure):
    """The full ladder of codes, shortest first."""
    stem = pinyin + (structure if use_structure else "")
    ladder = {pinyin, stem}

    letter_sets = [letters_for(p, names, hanzi) for p in parts]
    letter_sets = [s for s in letter_sets if s]

    for group in letter_sets:
        for letter in group:
            ladder.add(stem + letter)

    # Any two distinct components, in either order.
    for i, first in enumerate(letter_sets):
        for j, second in enumerate(letter_sets):
            if i == j:
                continue
            for a in first:
                for b in second:
                    ladder.add(stem + a + b)
    return ladder


HEADER = """# Rime dictionary
# encoding: utf-8
# Generated by data-tools/generate_zuxia.py - do not edit by hand.
---
name: {name}
version: "{version}"
sort: by_weight
{imports}...
"""


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--hanzi-data", type=pathlib.Path,
                    default=root / "sources/hanzi-dictionary.txt")
    ap.add_argument("--cjkvi-ids", type=pathlib.Path,
                    default=root / "sources/cjkvi-ids.txt")
    ap.add_argument("--charset", type=pathlib.Path,
                    default=root / "sources/8105.dict.yaml")
    ap.add_argument("--names", type=pathlib.Path,
                    default=root / "sources/component-names.yaml")
    ap.add_argument("--structure-map", type=pathlib.Path,
                    default=root / "sources/structure-overrides.tsv",
                    help="authoritative 字→结构码 map (see sync_structure.py)")
    ap.add_argument("--out-dir", type=pathlib.Path, default=root.parent / "data")
    ap.add_argument("--version", default="0.2.0")
    # The structure key sits between sound and form and is always present.
    #
    # Measured on this table, weighted by character frequency, per (code,
    # character) pair -- the same estimator 应物 publishes:
    #
    #     without structure   full code 5.02 keys, target first 97.04%,
    #                         2.96% still need picking, 44,561 rows
    #     with structure      full code 6.02 keys, target first 98.27%,
    #                         1.73% still need picking, 53,310 rows
    #
    # One more keystroke per character (0.603 -> 0.779 added keys on the
    # "keep adding until it is first" measure) roughly halves the picking.
    #
    # Its position is fixed rather than optional, and that is what keeps the
    # ladder unambiguous: `qingz` can only be 清's structure key, never a
    # component named 竹/足/走. It also keeps every code a 应物 typist already
    # knows: 清 qingzs, 情 qingzx, 应 yingbg are all still valid rungs here.
    ap.add_argument("--no-structure", dest="structure", action="store_false",
                    help="drop the structure key (comparison build only)")
    ap.set_defaults(structure=True)
    ap.add_argument("--report", type=pathlib.Path)
    args = ap.parse_args()

    hanzi = load_hanzi(args.hanzi_data)
    ids = load_ids(args.cjkvi_ids)
    names = load_names(args.names)
    charset = load_charset(args.charset)
    overrides = load_structure_overrides(args.structure_map)

    rows: dict[tuple[str, str], int] = {}
    stats = collections.Counter()
    # 结构码分布得报两种口径。未加权的那份看着「独体很少」，加权之后完全
    # 是另一回事 —— 一、人、口、山 这些最常用的字大半是独体。README 引过
    # 只有未加权的那一列且没标口径，外部复审据此判定那张表在误导读者。
    structure_weight: collections.Counter = collections.Counter()
    per_char_codes: list[int] = []
    unnamed: collections.Counter = collections.Counter()

    for char, pinyin, weight in charset:
        source = (hanzi.get(char) or {}).get("decomposition") or ids.get(char, "")
        structure = classify_structure(parse_ids(source), char, overrides)
        stats[f"structure_{structure}"] += 1
        structure_weight[structure] += weight

        parts = components_of(char, hanzi, ids)
        usable = [p for p in parts if letters_for(p, names, hanzi)]
        for p in parts:
            if not letters_for(p, names, hanzi):
                unnamed[p] += 1

        if len(usable) >= 2:
            stats["two_or_more_components"] += 1
        elif len(usable) == 1:
            stats["one_component"] += 1
        else:
            stats["no_component"] += 1

        codes = codes_for(pinyin, structure, usable, names, hanzi,
                          args.structure)
        per_char_codes.append(len(codes))
        for code in codes:
            key = (char, code)
            rows[key] = max(rows.get(key, 0), weight)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out = args.out_dir / "zuxia.dict.yaml"
    with out.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(HEADER.format(name="zuxia", version=args.version,
                                   imports=""))
        for (char, code), weight in sorted(
            rows.items(), key=lambda x: (x[0][1], -x[1], x[0][0])
        ):
            handle.write(f"{char}\t{code}\t{weight}\n")

    report = {
        "characters": len(charset),
        "rows": len(rows),
        "codes_per_character_avg": round(sum(per_char_codes) / len(per_char_codes), 2),
        "codes_per_character_max": max(per_char_codes),
        "structure": {k[10:]: v for k, v in stats.items() if k.startswith("structure_")},
        # 口径：上面 structure 是逐（字，读音）对的计数（与 characters 同分母），
        # 下面这份是同一批对按字频加权后的占比（百分数，两位小数）。
        "structure_weighted_pct": {
            k: round(100 * v / max(sum(structure_weight.values()), 1), 2)
            for k, v in sorted(structure_weight.items())
        },
        "two_or_more_components": stats["two_or_more_components"],
        "one_component": stats["one_component"],
        "no_component": stats["no_component"],
        "components_without_a_name": len(unnamed),
        "components_without_a_name_top": unnamed.most_common(20),
        "use_structure_key": args.structure,
        "structure_overrides_applied": len(
            {c for c, _, _ in charset} & set(overrides)),
    }
    text = json.dumps(report, ensure_ascii=False, indent=2)
    # The console may be on a legacy code page; the report is UTF-8 regardless.
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):
        pass
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
