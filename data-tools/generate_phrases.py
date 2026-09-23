#!/usr/bin/env python3
"""Build the deterministic Hengma 2–12 character phrase dictionary.

Code forms (all lowercase ASCII):

1. continuous full pinyin
2. full pinyin + structure/radical pair for the earliest discriminating
   ambiguous character
3. only when level 2 still collides, append one more structure/radical pair

The generator never uses a language model, sentence prediction, user history,
or network access. Source weights define a stable candidate order.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MANUAL_ENTRIES = [
    ("今天去哪了", "jin tian qu na le", 5_000_000),
    ("今天去拿了", "jin tian qu na le", 500_000),
    ("数字法治", "shu zi fa zhi", 2_000_000),
    ("中华民族共同体意识", "zhong hua min zu gong tong ti yi shi", 2_000_000),
    (
        "铸牢中华民族共同体意识",
        "zhu lao zhong hua min zu gong tong ti yi shi",
        2_000_000,
    ),
]


@dataclass(frozen=True)
class Entry:
    text: str
    code: str
    syllables: tuple[str, ...]
    weight: int


@dataclass(frozen=True)
class Branch:
    entries: tuple[Entry, ...]
    suffix: str
    used_positions: tuple[int, ...]


def is_han(ch: str) -> bool:
    cp = ord(ch)
    return (
        0x3400 <= cp <= 0x4DBF
        or 0x4E00 <= cp <= 0x9FFF
        or 0x20000 <= cp <= 0x3134F
    )


def load_char_info(path: Path) -> dict[str, tuple[str, str | None]]:
    result: dict[str, tuple[str, str | None]] = {}
    with path.open(encoding="utf-8") as source:
        for line in source:
            row = json.loads(line)
            result[row["character"]] = (row["structure"], row.get("cue"))
    return result


def load_rime_phrases(
    path: Path,
    char_info: dict[str, tuple[str, str | None]],
    max_length: int,
) -> list[Entry]:
    entries: dict[tuple[str, str, tuple[str, ...]], int] = {}
    body = False
    with path.open(encoding="utf-8") as source:
        for raw in source:
            line = raw.rstrip("\n")
            if line == "...":
                body = True
                continue
            if not body or not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            text = fields[0]
            syllables = tuple(fields[1].split())
            if not (2 <= len(text) <= max_length):
                continue
            if len(syllables) != len(text):
                continue
            if not all(is_han(ch) and ch in char_info for ch in text):
                continue
            if not all(re.fullmatch(r"[a-zv]+", item) for item in syllables):
                continue
            try:
                weight = int(fields[2]) if len(fields) > 2 else 1
            except ValueError:
                weight = 1
            key = (text, "".join(syllables), syllables)
            entries[key] = max(entries.get(key, 0), max(1, weight))

    for text, pinyin, weight in MANUAL_ENTRIES:
        syllables = tuple(pinyin.split())
        if len(text) <= max_length and all(ch in char_info for ch in text):
            key = (text, "".join(syllables), syllables)
            entries[key] = max(entries.get(key, 0), weight)

    return [
        Entry(text, code, syllables, weight)
        for (text, code, syllables), weight in entries.items()
    ]


def first_difference(entries: Iterable[Entry]) -> int | None:
    """Return the first character position whose text differs.

    Kept as a public helper for analysis and tests. A missing character caused
    by different phrase lengths counts as a difference.
    """
    rows = list(entries)
    if len(rows) < 2:
        return None
    for index in range(max(len(entry.text) for entry in rows)):
        values = {
            entry.text[index] if index < len(entry.text) else None
            for entry in rows
        }
        if len(values) > 1:
            return index
    return None


def pair_at(
    entry: Entry,
    index: int,
    char_info: dict[str, tuple[str, str | None]],
) -> str | None:
    if index >= len(entry.text):
        return None
    structure, cue = char_info[entry.text[index]]
    if not structure or not cue:
        return None
    pair = structure + cue
    return pair if re.fullmatch(r"[zsbpd][a-z]", pair) else None


def choose_split_position(
    entries: tuple[Entry, ...],
    char_info: dict[str, tuple[str, str | None]],
    used_positions: tuple[int, ...],
) -> int | None:
    """Pick the earliest *discriminating* ambiguous character.

    A position is useful only if every branch has a structure/radical pair and
    those pairs actually divide the candidates. This avoids asking the user to
    type a suffix that cannot change the candidate set.
    """
    used = set(used_positions)
    max_length = max(len(entry.text) for entry in entries)
    for index in range(max_length):
        if index in used:
            continue
        characters = {
            entry.text[index] if index < len(entry.text) else None
            for entry in entries
        }
        if len(characters) < 2:
            continue
        pairs = [pair_at(entry, index, char_info) for entry in entries]
        if any(pair is None for pair in pairs):
            continue
        if len(set(pairs)) > 1:
            return index
    return None


def disambiguate_group(
    entries: list[Entry],
    char_info: dict[str, tuple[str, str | None]],
    max_levels: int = 2,
) -> tuple[dict[Entry, list[str]], dict[Entry, str], list[dict]]:
    """Return generated extension codes and each entry's most precise code."""
    generated: dict[Entry, list[str]] = defaultdict(list)
    effective: dict[Entry, str] = {}
    decisions: list[dict] = []

    if len(entries) < 2 or max_levels <= 0:
        return generated, {entry: entry.code for entry in entries}, decisions

    branches = [Branch(tuple(entries), "", ())]
    for level in range(1, max_levels + 1):
        next_branches: list[Branch] = []
        for branch in branches:
            if len(branch.entries) < 2:
                effective[branch.entries[0]] = (
                    branch.entries[0].code + branch.suffix
                )
                continue

            index = choose_split_position(
                branch.entries, char_info, branch.used_positions
            )
            if index is None:
                for entry in branch.entries:
                    effective[entry] = entry.code + branch.suffix
                continue

            partitions: dict[str, list[Entry]] = defaultdict(list)
            for entry in branch.entries:
                pair = pair_at(entry, index, char_info)
                assert pair is not None
                suffix = branch.suffix + pair
                generated[entry].append(entry.code + suffix)
                partitions[pair].append(entry)

            decisions.append(
                {
                    "level": level,
                    "position": index,
                    "candidate_count": len(branch.entries),
                    "partition_count": len(partitions),
                }
            )
            for pair, subset in partitions.items():
                suffix = branch.suffix + pair
                if len(subset) == 1 or level == max_levels:
                    for entry in subset:
                        effective[entry] = entry.code + suffix
                else:
                    next_branches.append(
                        Branch(
                            tuple(subset),
                            suffix,
                            branch.used_positions + (index,),
                        )
                    )
        branches = next_branches
        if not branches:
            break

    for branch in branches:
        for entry in branch.entries:
            effective.setdefault(entry, entry.code + branch.suffix)
    for entry in entries:
        effective.setdefault(entry, entry.code)
    return generated, effective, decisions


def build_rows(
    entries: list[Entry],
    char_info: dict[str, tuple[str, str | None]],
    max_levels: int,
):
    rows: dict[tuple[str, str], int] = {}
    groups: dict[str, list[Entry]] = defaultdict(list)
    for entry in entries:
        rows[(entry.text, entry.code)] = max(
            rows.get((entry.text, entry.code), 0), entry.weight
        )
        # Continuous pinyin has no syllable separators. Group on the actual
        # keystroke string, including cross-length segmentation collisions.
        groups[entry.code].append(entry)

    effective: dict[Entry, str] = {}
    decisions: list[dict] = []
    level_rows = defaultdict(int)
    for group in groups.values():
        generated, group_effective, group_decisions = disambiguate_group(
            group, char_info, max_levels=max_levels
        )
        effective.update(group_effective)
        decisions.extend(group_decisions)
        for entry, codes in generated.items():
            for stage, code in enumerate(codes, start=1):
                rows[(entry.text, code)] = max(
                    rows.get((entry.text, code), 0), entry.weight
                )
                level_rows[stage] += 1
    return rows, effective, decisions, level_rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-dict", type=Path, required=True)
    parser.add_argument("--char-metadata", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--effective-out", type=Path)
    parser.add_argument("--version", default="0.3.0")
    parser.add_argument("--max-length", type=int, default=12)
    parser.add_argument("--max-levels", type=int, default=2, choices=(1, 2))
    args = parser.parse_args()

    char_info = load_char_info(args.char_metadata)
    entries = load_rime_phrases(args.base_dict, char_info, args.max_length)
    rows, effective, decisions, level_rows = build_rows(
        entries, char_info, args.max_levels
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            "# Rime dictionary\n"
            "# encoding: utf-8\n"
            "# Static 2-12 character Hengma phrases; no language model.\n"
            "---\n"
            "name: hengma_phrases\n"
            f'version: "{args.version}"\n'
            "sort: by_weight\n"
            "...\n"
        )
        for (text, code), weight in sorted(
            rows.items(), key=lambda item: (item[0][1], -item[1], item[0][0])
        ):
            output.write(f"{text}\t{code}\t{weight}\n")

    if args.effective_out:
        args.effective_out.parent.mkdir(parents=True, exist_ok=True)
        with args.effective_out.open("w", encoding="utf-8", newline="\n") as f:
            f.write("text\tplain_code\teffective_code\tlength\tweight\n")
            for entry in sorted(
                entries, key=lambda item: (item.code, -item.weight, item.text)
            ):
                f.write(
                    f"{entry.text}\t{entry.code}\t{effective[entry]}\t"
                    f"{len(entry.text)}\t{entry.weight}\n"
                )

    final_groups: dict[str, set[str]] = defaultdict(set)
    for entry, code in effective.items():
        final_groups[code].add(entry.text)
    residual = sum(len(texts) > 1 for texts in final_groups.values())
    examples = {
        entry.text: {
            "plain": entry.code,
            "effective": effective[entry],
        }
        for entry in entries
        if entry.text in {"今天去哪了", "今天去拿了"}
    }
    report = {
        "source_phrases": len(entries),
        "dictionary_rows": len(rows),
        "max_levels": args.max_levels,
        "extension_rows_by_stage": dict(sorted(level_rows.items())),
        "decision_nodes_by_level": {
            str(level): sum(item["level"] == level for item in decisions)
            for level in range(1, args.max_levels + 1)
        },
        "effective_code_collision_groups": residual,
        "max_code_length": max(len(code) for _, code in rows),
        "examples": examples,
        "output": str(args.out),
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
