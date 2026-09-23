#!/usr/bin/env python3
"""Build the component naming worksheet — the backbone of 足下 encoding.

In 足下, the form code is the initial of a component's **spoken name**, not of
its own reading. 宀 reads gài but everyone calls it 宝盖, so its letter is b;
氵 is 水, so s. Getting those names right is the whole job, because a code
nobody can guess is a code nobody can type.

This script does not invent names. It gathers every component the character
set decomposes into, ranks them by how many characters they reach, and marks
what is already known versus what a person still has to decide. The output is
a TSV meant to be edited by hand and read back by the generator.

Columns:
  component   the component itself
  name        its spoken name -- THE field to fill in
  letter      derived from the name's pinyin initial; blank until named
  status      settled / proposed / needed
  chars       how many characters contain it
  readings    its own pinyin, for reference only
  examples    a few characters that contain it
"""
from __future__ import annotations

import argparse
import collections
import io
import json
import pathlib
import unicodedata

BINARY_IDS = set("⿰⿱⿴⿵⿶⿷⿸⿹⿺⿻")
TERNARY_IDS = set("⿲⿳")

# Markers and stray latin letters that leak out of the IDS sources; they are
# not components and must never reach the worksheet.
NOT_A_COMPONENT = set("？?[]{}()0123456789"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "abcdefghijklmnopqrstuvwxyz")

# Names settled so far. Variants of one component share a name, which is the
# point: 氵 and 水 are both "水", so both give s.
SETTLED_NAMES: dict[str, str] = {
    # water, hand, heart, speech -- the high-frequency radicals
    "氵": "水", "水": "水", "氺": "水",
    "扌": "手", "手": "手",
    "忄": "心", "心": "心", "⺗": "心",
    "讠": "言", "言": "言",
    "钅": "金", "金": "金",
    "艹": "草",
    "亻": "人", "人": "人",
    "宀": "宝盖", "冖": "宝盖",
    "疒": "病",
    "辶": "走", "廴": "走", "走": "走",
    "阝": "耳刀",
    "礻": "示", "示": "示",
    "衤": "衣", "衣": "衣",
    "饣": "食", "食": "食",
    "纟": "丝", "糹": "丝", "糸": "丝",
    "刂": "刀", "刀": "刀",
    "灬": "火", "火": "火",
    "冫": "冰",
    "犭": "犬", "犬": "犬",
    "攵": "文", "攴": "文",
    "⺮": "竹", "竹": "竹",
    "罒": "网", "网": "网",
    "囗": "框",
    "⺼": "月", "月": "月",
    "广": "广", "门": "门", "門": "门",
    "口": "口", "日": "日", "木": "木", "土": "土", "石": "石",
    "米": "米", "女": "女", "王": "王", "玉": "玉", "目": "目",
    "虫": "虫", "禾": "禾", "足": "足", "子": "子", "大": "大",
    "小": "小", "山": "山", "雨": "雨", "耳": "耳", "车": "车",
    "車": "车", "马": "马", "馬": "马", "鱼": "鱼", "魚": "鱼",
    "鸟": "鸟", "鳥": "鸟", "页": "页", "頁": "页",
    "贝": "贝", "貝": "贝", "田": "田",
}


def strip_tone(text: str) -> str:
    text = text.lower().replace("ü", "v").replace("u:", "v")
    decomposed = unicodedata.normalize("NFD", text)
    return "".join(c for c in decomposed if not unicodedata.combining(c))


def load_hanzi(path: pathlib.Path) -> dict[str, dict]:
    rows: dict[str, dict] = {}
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("character"):
                rows[row["character"]] = row
    return rows


def load_ids(path: pathlib.Path) -> dict[str, str]:
    result: dict[str, str] = {}
    with path.open(encoding="utf-8") as handle:
        for raw in handle:
            if raw.startswith("#"):
                continue
            parts = raw.rstrip("\n").split("\t")
            if len(parts) >= 3 and parts[1] not in result:
                result[parts[1]] = parts[2]
    return result


def load_charset(path: pathlib.Path) -> list[str]:
    chars: list[str] = []
    started = False
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if not started:
                if line.startswith("..."):
                    started = True
                continue
            parts = line.rstrip("\n").split("\t")
            if parts and parts[0]:
                chars.append(parts[0])
    return list(dict.fromkeys(chars))


def decompose(char: str, hanzi: dict, ids: dict) -> list[str]:
    row = hanzi.get(char) or {}
    text = row.get("decomposition") or ""
    if not text or text == "？":
        text = ids.get(char, "")
    return [c for c in text
            if c not in BINARY_IDS and c not in TERNARY_IDS
            and c not in NOT_A_COMPONENT and c != char]


def readings_of(char: str, hanzi: dict) -> list[str]:
    return [strip_tone(p) for p in (hanzi.get(char) or {}).get("pinyin", []) if p]


def letter_for(name: str, hanzi: dict) -> str:
    """The pinyin initial of a spoken name. Multi-character names use the
    first character, which is how 宝盖 becomes b and 走之 becomes z."""
    if not name:
        return ""
    readings = readings_of(name[0], hanzi)
    return readings[0][0] if readings and readings[0] else ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    root = pathlib.Path(__file__).resolve().parent
    ap.add_argument("--hanzi-data", type=pathlib.Path,
                    default=root / "sources/hanzi-dictionary.txt")
    ap.add_argument("--cjkvi-ids", type=pathlib.Path,
                    default=root / "sources/cjkvi-ids.txt")
    ap.add_argument("--charset", type=pathlib.Path,
                    default=root / "sources/8105.dict.yaml")
    ap.add_argument("--out", type=pathlib.Path,
                    default=root / "sources/component-names.tsv")
    args = ap.parse_args()

    hanzi = load_hanzi(args.hanzi_data)
    ids = load_ids(args.cjkvi_ids)
    charset = load_charset(args.charset)

    reach: collections.Counter = collections.Counter()
    examples: dict[str, list[str]] = collections.defaultdict(list)
    for char in charset:
        for part in decompose(char, hanzi, ids):
            reach[part] += 1
            if len(examples[part]) < 5:
                examples[part].append(char)

    lines = [
        "# 应物音形足下输入法 —— 部件命名表",
        "#",
        "# 形码取部件「名称」的拼音首字母，不是部件本身的读音：",
        "#   宀 读 gài，但通称「宝盖」，故取 b",
        "#   氵 通称「水」，故取 s",
        "# 名称起错，码就没人猜得到——这张表是整个方案的地基。",
        "#",
        "# status:  settled  已确定",
        "#          proposed 按部件本身读音暂拟，需人工确认",
        "#          needed   既无通称也无读音，必须人工指定",
        "#",
        "# 只需填写 name 一列；letter 由 name 的拼音首字母自动推导。",
        "#",
        "component\tname\tletter\tstatus\tchars\treadings\texamples",
    ]

    stats = collections.Counter()
    for part, count in reach.most_common():
        readings = readings_of(part, hanzi)
        if part in SETTLED_NAMES:
            name, status = SETTLED_NAMES[part], "settled"
        elif readings:
            name, status = part, "proposed"
        else:
            name, status = "", "needed"
        stats[status] += 1
        letter = letter_for(name, hanzi) if name else ""
        if name and not letter:
            status = "needed"
        lines.append("\t".join([
            part, name, letter, status, str(count),
            "/".join(readings), "".join(examples[part]),
        ]))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with io.open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")

    print(f"{args.out}: {len(reach)} components")
    for status in ("settled", "proposed", "needed"):
        print(f"  {status:<9}{stats[status]:5d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
