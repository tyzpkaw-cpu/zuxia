#!/usr/bin/env python3
"""Generate the 足下 single-character dictionary.

足下 encodes a character as sound plus form, where the form is carried by the
character's own parts rather than by its radical alone:

    full pinyin  →  + structure  →  + one component letter  →  + two

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

SINGLE_STRUCTURE_OVERRIDES = set(
    "中木本末未米术朱束东车申甲由田王玉井开丰手牛羊生年午果来"
    "夫天大太犬丈支十干于土士工〇"
)


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
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if not started:
                if line.startswith("..."):
                    started = True
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2 and parts[0] and parts[1]:
                weight = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 1
                out.append((parts[0], strip_tone(parts[1]), weight))
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


def classify_structure(node, character: str) -> str:
    if character in SINGLE_STRUCTURE_OVERRIDES:
        return "d"
    if node is None:
        # No decomposition at all. "Unknown" is not the same claim as 独体,
        # so it joins the catch-all rather than being called single-component.
        return "p"
    op, children = node
    if not children:
        return "d"
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
    ap.add_argument("--out-dir", type=pathlib.Path, default=root.parent / "data")
    ap.add_argument("--version", default="0.1.0")
    # Measured: at five keys, pinyin + two component letters reaches the
    # target first 94.67% of the time, against 94.21% for pinyin + structure
    # + radical -- the same accuracy without needing to know which part is
    # the radical or what it is called. Adding the structure key on top costs
    # a keystroke and buys 2.13 points, so it is off by default. It cannot be
    # optional per-character: `qingz` would then be ambiguous between the
    # structure z and a component named 竹/足/走.
    ap.add_argument("--structure", action="store_true",
                    help="insert the structure key between sound and form")
    ap.add_argument("--report", type=pathlib.Path)
    args = ap.parse_args()

    hanzi = load_hanzi(args.hanzi_data)
    ids = load_ids(args.cjkvi_ids)
    names = load_names(args.names)
    charset = load_charset(args.charset)

    rows: dict[tuple[str, str], int] = {}
    stats = collections.Counter()
    per_char_codes: list[int] = []
    unnamed: collections.Counter = collections.Counter()

    for char, pinyin, weight in charset:
        source = (hanzi.get(char) or {}).get("decomposition") or ids.get(char, "")
        structure = classify_structure(parse_ids(source), char)
        stats[f"structure_{structure}"] += 1

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
        "two_or_more_components": stats["two_or_more_components"],
        "one_component": stats["one_component"],
        "no_component": stats["no_component"],
        "components_without_a_name": len(unnamed),
        "components_without_a_name_top": unnamed.most_common(20),
        "use_structure_key": args.structure,
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
