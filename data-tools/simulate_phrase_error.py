#!/usr/bin/env python3
"""Evaluate fixed-dictionary phrase collisions without a language model.

The input TSV is emitted by generate_phrases.py --effective-out. Each phrase is
considered an intended target in turn. Candidates sharing a code are sorted by
fixed source weight; automatic top-1 commit is compared with the target using
character edit distance. If the target appears in the top K, the scan-top-K
scenario assumes that the user selects it correctly.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def edit_distance(a: str, b: str) -> int:
    previous = list(range(len(b) + 1))
    for i, left in enumerate(a, 1):
        current = [i]
        for j, right in enumerate(b, 1):
            current.append(
                min(
                    current[-1] + 1,
                    previous[j] + 1,
                    previous[j - 1] + (left != right),
                )
            )
        previous = current
    return previous[-1]


def load_rows(path: Path) -> list[dict]:
    with path.open(encoding="utf-8", newline="") as source:
        rows = []
        for row in csv.DictReader(source, delimiter="\t"):
            row["weight"] = int(row["weight"])
            row["length"] = int(row["length"])
            rows.append(row)
        return rows


def evaluate(rows: list[dict], code_field: str) -> dict:
    # Collapse accidental duplicates by (code, text), retaining max weight.
    collapsed: dict[tuple[str, str], dict] = {}
    for row in rows:
        key = (row[code_field], row["text"])
        if key not in collapsed or row["weight"] > collapsed[key]["weight"]:
            collapsed[key] = row

    groups: dict[str, list[dict]] = defaultdict(list)
    for row in collapsed.values():
        groups[row[code_field]].append(row)
    for group in groups.values():
        group.sort(key=lambda item: (-item["weight"], item["text"]))

    total_weight = 0
    total_weighted_chars = 0
    wrong_selection_weight = 0
    weighted_edit = 0
    weighted_edit_top3 = 0
    collision_weight = 0
    max_candidates = 0
    for group in groups.values():
        predicted = group[0]["text"]
        max_candidates = max(max_candidates, len(group))
        for rank, row in enumerate(group, 1):
            weight = row["weight"]
            total_weight += weight
            total_weighted_chars += weight * len(row["text"])
            distance = edit_distance(row["text"], predicted)
            weighted_edit += weight * distance
            if rank > 3:
                weighted_edit_top3 += weight * distance
            if rank > 1:
                wrong_selection_weight += weight
            if len(group) > 1:
                collision_weight += weight

    return {
        "records": len(collapsed),
        "codes": len(groups),
        "weighted_top1_selection_error": wrong_selection_weight / total_weight,
        "weighted_wrong_character_rate": weighted_edit / total_weighted_chars,
        "weighted_wrong_character_rate_if_scan_top3": (
            weighted_edit_top3 / total_weighted_chars
        ),
        "weighted_collision_mass": collision_weight / total_weight,
        "max_candidates": max_candidates,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--effective-tsv", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    rows = load_rows(args.effective_tsv)
    result = {
        "method": (
            "Fixed source weight proxy; automatic highest-weight candidate; "
            "weighted character edit distance."
        ),
        "plain_continuous_pinyin": evaluate(rows, "plain_code"),
        "up_to_two_disambiguation_pairs": evaluate(rows, "effective_code"),
    }
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
