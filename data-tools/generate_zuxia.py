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
import itertools
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

# 部件名称的权威来源，和结构码用 GF 0013—2009 是同一个家族的标准。
#
# 0.2.0 的毛病：名称表查不到的部件，letters_for() 退回部件「自己的读音」。于是
# 龺 取 g（gàn）、㐬 取 l（liú）、尃 取 f（fū）、帀 取 z（zā）。实测 1586 种顶层
# 部件里有 1261 种没有名称，2365 字（28.90% 字数 / 34.46% 字频）整个字一个可猜
# 部件都没有 —— 这些码没人猜得出来，也就没人打得出来。
#
# GF 0014—2009《现代常用字部件及部件名称规范》给出 441 组 514 个部件的官方名称
# （宝盖、草字头、病字框、倒八、私字边……）。并入之后那 2365 字降到 72 字
# （0.88% / 4.72%）。
#
# 并入方式是**追加**而不是覆盖，两层都重要：
#   * 人工名称在前 —— sources/component-names.yaml 是逐条核过的，国标的通名
#     （比如把 疒 叫「病字框」而人工叫「病」）不该把它顶掉。
#   * 部件自己的读音保留在列表里 —— 否则 0.2.0 那些「不可猜但已经被记住」的码
#     （朝 chaozg）就会消失。实测不保留会让 174 个字丢掉旧码。
GF0014_PATH = (pathlib.Path(__file__).resolve().parent
               / "sources/gf0014-components.txt")

# 一个部件最多拆几层。
#
# 实测（乱序、最多三个部件、按字频加权看「打满后该字有没有只属于它的码」）：
#   只拆一层  275,268 行 4.84 MB  打满无唯一码 1410 字 17.23% / 字频 12.43%
#   拆两层    330,097 行 5.83 MB              1452 字 17.74% / 13.17%
#   拆四层    332,558 行 5.87 MB              1458 字 17.82% / 13.19%
# 拆得越深越大*而且*越不唯一：再往下拆出来的都是低信息量的笔画件，它们让不同的
# 字长得更像。所以只拆一层。
MAX_COMPONENT_DEPTH = 1

# 一个码最多带几个部件字母。
#
# 0.2.0 是 2。作者实测反馈「目前对于汉字的部件码还没有穷尽」「需要再补一次部件
# 码」，两条都成立。同口径实测：
#   只拆一层 / 最多两个   114,801 行  打满无唯一码 2417 字 29.54% / 字频 24.39%
#   只拆一层 / 最多三个   275,268 行              1410 字 17.23% / 字频 12.43%
#   只拆一层 / 最多四个   （行数再 +40%，只多救 59 个字）
# 注意第一行：**拆了层却不给第三个部件，按字频比 0.2.0 的 19.06% 还差**。递归把
# 一个有辨识度的整体部件（览 l）换成辨识度更低的子部件（见 j），两个字母补不回
# 来，高频字受害最重。所以这两件事必须同一次上线，缺一个就是倒退。
#
# 四个不做，是因为乱序的代价是排列数（n 个部件取 k 个有 n!/(n−k)! 种顺序），
# 从三到四多花四万行只多救 59 个字。顺序不限是作者定的口径，不改。
MAX_COMPONENTS_PER_CODE = 3

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

# For these characters the cjkvi-ids decomposition gives more guessable
# components than hanzi-dictionary.  components_of() will use cjkvi-ids
# instead of the hanzi-dictionary row.
# 衍: hanzi-dict ⿻行氵 (infix) → takes 行/氵; cjkvi ⿲彳氵亍 → takes 彳/氵/亍 (user decision 2026-10-01)
CJKVI_PREFERRED: set[str] = {"衍"}


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


def load_gf0014(path: pathlib.Path = GF0014_PATH) -> dict[str, list[str]]:
    """GF 0014—2009 现代常用字部件表：`部件 TAB 序号 TAB 组号 TAB 名称 TAB 例字`。

    名称列用 `/` 分隔多个通行叫法（`釆/番字头`）。部件列写成 `{…}` 的那 30 条
    没有 Unicode 码位，只能用 IDS 描述，生成器拿不到，跳过并计数。
    """
    out: dict[str, list[str]] = {}
    skipped = 0
    if not path.exists():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("#") or not raw.strip():
            continue
        fields = raw.split("\t")
        if len(fields) < 4:
            continue
        part, label = fields[0].strip(), fields[3].strip()
        if not part or not label:
            continue
        if part.startswith("{"):
            skipped += 1
            continue
        bucket = out.setdefault(part, [])
        for name in label.split("/"):
            name = name.strip()
            if name and name not in bucket:
                bucket.append(name)
    if skipped:
        print(f"GF 0014: skipped {skipped} components that have no code point",
              file=sys.stderr)
    return out


def merge_names(curated: dict[str, list[str]],
                standard: dict[str, list[str]]) -> dict[str, list[str]]:
    """人工名称 + 国标名称 + 部件自身，按这个优先级合成一张名称表。

    追加而不覆盖。部件自身留在列表里是为了让 0.2.0 的每一条码都还在 ——
    `audit_zuxia.py` 有一条断言机械地盯着这件事。
    """
    out: dict[str, list[str]] = {k: list(v) for k, v in curated.items()}
    for part, labels in standard.items():
        bucket = out.setdefault(part, [part])
        for label in labels:
            if label not in bucket:
                bucket.append(label)
    return out


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
    if char in CJKVI_PREFERRED:
        text = ids.get(char, "")
    else:
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


def expand_components(char: str, hanzi: dict, ids: dict, names: dict,
                      depth: int = MAX_COMPONENT_DEPTH) -> list[str]:
    """顶层部件，外加「没有名称的部件」再拆一层的结果。

    0.2.0 只取 components_of()，也就是只拆一层，不往下走。后果是一整类部件
    打不出来：傅 只有 亻 和 尃，拿不到 甫 和 寸；朝 只有 龺 和 月，拿不到 十
    和 日；梳 只有 木 和 㐬。实测递归之后可用部件会变多的字有 3047 个
    （37.24% 字数 / 13.33% 字频）。

    停在「名称表里有名字的部件」—— 名称表就是停止条件，这也是为什么并入
    GF 0014 必须先做：表越全，拆得越浅，码表越小而且越可猜。

    顶层部件本身**一定保留**（哪怕它没有名字、字母是它自己的生僻读音），
    否则 0.2.0 已经被记住的码会消失。
    """
    out: list[str] = []

    def visit(part: str, level: int) -> None:
        if part != char and part not in out:
            out.append(part)
        if level >= depth or part in names or part in SINGLE_STRUCTURE_OVERRIDES:
            return
        row = hanzi.get(part) or {}
        text = row.get("decomposition") or ""
        if not text or text == "？":
            text = ids.get(part, "")
        for child in text:
            if child in BINARY_IDS or child in TERNARY_IDS:
                continue
            if child in NOT_A_COMPONENT or child == part:
                continue
            visit(child, level + 1)

    for top in components_of(char, hanzi, ids):
        visit(top, 0)
    return out


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


def codes_for(pinyin, structure, parts, names, hanzi, use_structure,
              max_components: int = MAX_COMPONENTS_PER_CODE):
    """The full ladder of codes, shortest first."""
    stem = pinyin + (structure if use_structure else "")
    ladder = {pinyin, stem}

    letter_sets = [letters_for(p, names, hanzi) for p in parts]
    letter_sets = [s for s in letter_sets if s]

    # 任意 1..max_components 个**互不相同**的部件，顺序不限。
    #
    # 顺序不限是作者定的口径：一个人看着字说不出「哪个部件算第一个」，所以
    # 不能要求他按某个顺序敲。代价是按排列数展开（n 个部件取 k 个有
    # n!/(n−k)! 种顺序），这也正是 max_components 卡在 3 的原因。
    width = min(max_components, len(letter_sets))
    for size in range(1, width + 1):
        for indices in itertools.permutations(range(len(letter_sets)), size):
            for combo in itertools.product(*[sorted(letter_sets[i])
                                             for i in indices]):
                ladder.add(stem + "".join(combo))
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
    ap.add_argument("--gf0014", type=pathlib.Path, default=GF0014_PATH,
                    help="GF 0014—2009 部件名称表")
    ap.add_argument("--no-gf0014", dest="use_gf0014", action="store_false",
                    help="不并入国标部件名称（对照构建用）")
    ap.set_defaults(use_gf0014=True)
    ap.add_argument("--max-components", type=int,
                    default=MAX_COMPONENTS_PER_CODE,
                    help="一个码最多带几个部件字母（对照构建用；出货值是 3）")
    ap.add_argument("--component-depth", type=int, default=MAX_COMPONENT_DEPTH,
                    help="没有名称的部件再拆几层（对照构建用；出货值是 1）")
    ap.add_argument("--structure-map", type=pathlib.Path,
                    default=root / "sources/structure-overrides.tsv",
                    help="authoritative 字→结构码 map (see sync_structure.py)")
    ap.add_argument("--out-dir", type=pathlib.Path, default=root.parent / "data")
    ap.add_argument("--version", default="0.3.0")
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
    curated = load_names(args.names)
    standard = load_gf0014(args.gf0014) if args.use_gf0014 else {}
    names = merge_names(curated, standard)
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
    # parts_rows: collected for zuxia.parts.tsv export (char -> (structure, parts_with_names))
    parts_rows: list[tuple[str, str, list[tuple[str, list[str]]]]] = []

    for char, pinyin, weight in charset:
        source = (hanzi.get(char) or {}).get("decomposition") or ids.get(char, "")
        structure = classify_structure(parse_ids(source), char, overrides)
        stats[f"structure_{structure}"] += 1
        structure_weight[structure] += weight

        parts = expand_components(char, hanzi, ids, names,
                                  args.component_depth)
        usable = [p for p in parts if letters_for(p, names, hanzi)]
        for p in parts:
            if p not in names:
                unnamed[p] += 1

        # collect for parts.tsv (deduplicate per char across pinyins)
        parts_with_names = [(p, names.get(p, [])) for p in usable]
        parts_rows.append((char, structure, parts_with_names))

        if len(usable) >= 2:
            stats["two_or_more_components"] += 1
        elif len(usable) == 1:
            stats["one_component"] += 1
        else:
            stats["no_component"] += 1

        codes = codes_for(pinyin, structure, usable, names, hanzi,
                          args.structure, args.max_components)
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

    # Write zuxia.parts.tsv for the parts window (Task 7).
    # Format: char TAB structure TAB part1|name1,name2 TAB part2|... (one row per char)
    # Only the highest-weight row is kept when a char appears multiple times (rare).
    seen_chars: set[str] = set()
    parts_out = args.out_dir / "zuxia.parts.tsv"
    with parts_out.open("w", encoding="utf-8", newline="\n") as ph:
        ph.write("# Zuxia parts table - generated by generate_zuxia.py - do not edit\n")
        ph.write("# char<TAB>structure<TAB>part|name1,name2<TAB>...\n")
        for char, structure, pw in parts_rows:
            if char in seen_chars:
                continue
            seen_chars.add(char)
            cols = [char, structure]
            for part, part_names in pw:
                letter_set = sorted(letters_for(part, names, hanzi))
                names_str = ",".join(part_names) if part_names else part
                cols.append(f"{part}|{names_str}|{''.join(letter_set)}")
            ph.write("\t".join(cols) + "\n")

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
        # 并入国标之前 1261 种部件没有名称；之后只剩下「拆到底仍然无名」的几种。
        "gf0014_components_loaded": len(standard),
        "curated_components": len(curated),
        "max_components_per_code": args.max_components,
        "component_depth": args.component_depth,
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
