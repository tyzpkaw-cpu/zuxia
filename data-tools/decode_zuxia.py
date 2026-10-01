# -*- coding: utf-8 -*-
"""足下「列式码」解码器原型 —— 不依赖词库也能拼出任意词。

一个词的码是三段拼接，逐位左对齐：

    全拼串  +  逐位结构串  +  逐位部件串
    suyao      sz            cw          ->  苏瑶

这与单字规则同构（n=1 时退化成 su + s + c，即现有单字码），所以打字的人
只需要记一套规则。代价是 Rime 自带的 table_translator 切不动它：结构位和
部件位与它们描述的那个字并不相邻，必须按「列」而不是按「行」去配。

本文件就是那个按列配的解码器。它先把前缀切成拼音音节，再把剩下的尾巴按
位分派给各个字，最后逐位查单字索引、做定向搜索。验证通过后照此移植进
src/RimeEngine.cpp。

用法:
    python3 decode_zuxia.py suyaoszcw
    python3 decode_zuxia.py --bench            # 20 万词基准
"""
from __future__ import annotations

import argparse
import collections
import functools
import itertools
import math
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import generate_zuxia as g

ROOT = pathlib.Path(__file__).resolve().parent
STRUCTURE_KEYS = set("zsbpd")
# 一个字最多给三个部件，与单字方案的梯级一致。
#
# 0.2.0 卡在两个部件，作者真机反馈「部件码还没有穷尽」：很多字拆到第二层
# 还有可用的部件，只给两位就漏掉了，需要再补一次部件码。0.3.0 因此把上限
# 提到三位，与 generate_zuxia.MAX_COMPONENTS_PER_CODE 对齐。
MAX_COMPONENTS = 3
# 定向搜索宽度。逐位扩展时只留权重最高的这么多个前缀，避免长词组合爆炸。
BEAM = 400


class Index:
    """单字索引：(拼音, 结构, 部件字母…) -> 字。"""

    def __init__(self, hanzi, ids, names, charset, overrides):
        self.weight: dict[str, int] = {}
        self.by_py: dict[str, set[str]] = collections.defaultdict(set)
        self.by_pys: dict[tuple[str, str], set[str]] = collections.defaultdict(set)
        self.by_pysc: dict[tuple[str, str, str], set[str]] = collections.defaultdict(set)
        self.by_pyscc: dict[tuple[str, str, str, str], set[str]] = collections.defaultdict(set)
        self.by_pysccc: dict[tuple[str, str, str, str, str], set[str]] = collections.defaultdict(set)
        self.syllables: set[str] = set()

        for char, pinyin, weight in charset:
            src = (hanzi.get(char) or {}).get("decomposition") or ids.get(char, "")
            structure = g.classify_structure(g.parse_ids(src), char, overrides)
            groups = [g.letters_for(p, names, hanzi)
                      for p in g.expand_components(char, hanzi, ids, names)]
            groups = [s for s in groups if s]
            if not groups:
                # 32 个字（入 心 舟 女 …）拆不出部件。列式码要求每一位都能
                # 填，所以让它们用自己名字的首字母顶上：入 = ru p r。
                groups = [{pinyin[0]}]

            self.weight[char] = max(self.weight.get(char, 0), weight)
            self.syllables.add(pinyin)
            self.by_py[pinyin].add(char)
            self.by_pys[(pinyin, structure)].add(char)
            for group in groups:
                for letter in group:
                    self.by_pysc[(pinyin, structure, letter)].add(char)
            # 任意 1..MAX_COMPONENTS 个互不相同的部件，顺序不限 —— 与
            # generate_zuxia.codes_for 的排列展开同构。0.2.0 在这里自己写了
            # 一段「任意两个部件」的双重循环，0.3.0 删掉，改成按排列展开，
            # 这样第三位也能填进来，且口径与生成器、src/Decoder.cpp 一致。
            width = min(MAX_COMPONENTS, len(groups))
            for size in range(1, width + 1):
                for indices in itertools.permutations(range(len(groups)), size):
                    for combo in itertools.product(*[sorted(groups[i])
                                                     for i in indices]):
                        key = (pinyin, structure) + tuple(combo)
                        if len(combo) == 1:
                            self.by_pysc[key].add(char)
                        elif len(combo) == 2:
                            self.by_pyscc[key].add(char)
                        else:
                            self.by_pysccc[key].add(char)
            # 独部件字：第二位重写第一位，与 Speller.code 的规则对上。
            if len(groups) == 1:
                for a in groups[0]:
                    self.by_pyscc[(pinyin, structure, a, a)].add(char)

        self.max_syllable = max(len(s) for s in self.syllables)

    def lookup(self, syllable, structure, comps):
        """一个位置上所有还站得住的字。"""
        if structure is None:
            return self.by_py.get(syllable, frozenset())
        if not comps:
            return self.by_pys.get((syllable, structure), frozenset())
        if len(comps) == 1:
            return self.by_pysc.get((syllable, structure, comps[0]), frozenset())
        if len(comps) == 2:
            return self.by_pyscc.get((syllable, structure, comps[0], comps[1]),
                                     frozenset())
        return self.by_pysccc.get(
            (syllable, structure, comps[0], comps[1], comps[2]), frozenset())


def load_index(root: pathlib.Path = ROOT) -> Index:
    return Index(
        g.load_hanzi(root / "sources/hanzi-dictionary.txt"),
        g.load_ids(root / "sources/cjkvi-ids.txt"),
        g.merge_names(g.load_names(root / "sources/component-names.yaml"),
                      g.load_gf0014()),
        g.load_charset(root / "sources/8105.dict.yaml"),
        g.load_structure_overrides(root / "sources/structure-overrides.tsv"),
    )


def load_phrases(path: pathlib.Path, limit: int = 200000) -> dict[str, int]:
    rows = []
    body = False
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("..."):
                body = True
                continue
            if not body:
                continue
            cells = line.rstrip("\n").split("\t")
            if len(cells) >= 3 and len(cells[0]) >= 2:
                try:
                    rows.append((cells[0], int(cells[2])))
                except ValueError:
                    pass
    rows.sort(key=lambda x: -x[1])
    return dict(rows[:limit])


def bigrams_from(phrases: dict[str, int]) -> dict[str, float]:
    """字与字的相邻频次。

    没有它，纯拼时排序只能按单字频次，于是「杨志鹏」会输给「样直鹏」——
    每个字单看都更常见，连起来却没人这么写。二元组把「这两个字会不会挨在
    一起」补了回来，而且它是从同一份词库统计出来的，不需要额外语料。
    """
    out: dict[str, float] = collections.defaultdict(float)
    for word, freq in phrases.items():
        value = math.log(freq + 1)
        for a, b in zip(word, word[1:]):
            out[a + b] += value
    return out


def segmentations(text: str, index: Index, max_syllables: int = 12):
    """把前缀切成拼音音节，返回 (音节表, 尾巴) 的所有解。

    尾巴长度必须落在 [0, MAX_COMPONENTS+1 倍音节数] 内，否则这串码不可能是
    这么多个字 —— 这条约束就是列式码之所以可判定的原因。
    """
    out = []

    def walk(pos, acc):
        if len(acc) > max_syllables:
            return
        if acc:
            tail = text[pos:]
            if len(tail) <= (MAX_COMPONENTS + 1) * len(acc):
                out.append((tuple(acc), tail))
        for size in range(1, min(index.max_syllable, len(text) - pos) + 1):
            head = text[pos:pos + size]
            if head in index.syllables:
                acc.append(head)
                walk(pos + size, acc)
                acc.pop()

    walk(0, [])
    # 字多的解排前面：同一串码若能解成更长的词，那通常就是本意。
    out.sort(key=lambda x: (-len(x[0]), len(x[1])))
    return out


def constraints(syllables, tail):
    """把尾巴按列分派给每个字：第 i 位拿结构、第一部件、第二部件、第三部件。

    0.2.0 只分派到第二部件，第三位填不进来。0.3.0 把 MAX_COMPONENTS 提到
    三，这里的分列填充要能填到第三位 —— 作者真机反馈「部件码还没有穷尽」，
    需要再补一次部件码。
    """
    n = len(syllables)
    per = []
    for i in range(n):
        structure = tail[i] if i < len(tail) else None
        if structure is not None and structure not in STRUCTURE_KEYS:
            return None
        comps = []
        for level in range(MAX_COMPONENTS):
            at = (level + 1) * n + i
            if at < len(tail):
                comps.append(tail[at])
        # 没给结构却给了部件是不合法的：三段是左对齐逐位填的。
        if comps and structure is None:
            return None
        per.append((syllables[i], structure, comps))
    return per


# 二元组权重。1.0 时「连得上」和「字本身常见」同等重要；实测 1.0 最好。
BIGRAM_WEIGHT = 1.0


def decode(text: str, index: Index, prior: dict[str, int] | None = None,
           bigram: dict[str, float] | None = None, limit: int = 9):
    """返回 [(词, 分数, 是否在词库)]，分数越大越靠前。"""
    prior = prior or {}
    bigram = bigram or {}
    best: dict[str, tuple[int, float, bool]] = {}

    for syllables, tail in segmentations(text, index):
        per = constraints(syllables, tail)
        if per is None:
            continue
        # 这个切法把码填满到第几列。半列（尾巴除不尽字数）的切法码表根本
        # 出不来，记 -1 永远排在后面。与 src/Decoder.cpp 的 columns_filled
        # 同构 —— 判据是「码打得满」而不是「字多」：打 xuancibz 时
        # 「选词」把结构列填满了，「选此」只是碰巧也解得通。
        columns = (-1 if len(tail) % len(syllables)
                   else len(tail) // len(syllables))
        beam: list[tuple[str, float]] = [("", 0.0)]
        for syllable, structure, comps in per:
            chars = index.lookup(syllable, structure, comps)
            if not chars:
                beam = []
                break
            nxt = []
            for prefix, score in beam:
                for ch in chars:
                    step = math.log(max(index.weight.get(ch, 1), 1))
                    if prefix:
                        step += BIGRAM_WEIGHT * bigram.get(prefix[-1] + ch, 0.0)
                    nxt.append((prefix + ch, score + step))
            nxt.sort(key=lambda x: -x[1])
            beam = nxt[:BEAM]
        for word, score in beam:
            # 词库命中直接抬到另一个量级，非词库解按字频几何均值排。
            in_dict = word in prior
            value = (1e9 + math.log(prior[word] + 1)) if in_dict else score / len(word)
            old = best.get(word)
            if old is None or (columns, value) > (old[0], old[1]):
                best[word] = (columns, value, in_dict)

    ranked = sorted(best.items(), key=lambda kv: (-kv[1][0], -kv[1][1], kv[0]))
    return [(w, v, d) for w, (_, v, d) in ranked[:limit]]


# 最多允许把末尾这么多位当成「没打完／打错了」退回去重试。一个字最多多出
# 结构位加三个部件位，正好四位，所以「少打一个字的尾巴」一定落在 4 位以内。
#
# 0.2.0 是 3（结构位加两个部件位）。0.3.0 部件上限提到三位，这里跟着改成
# 4，与 generate_zuxia.MAX_FALLBACK_TAIL 对齐。
MAX_FALLBACK_TAIL = 4


def decode_fallback(text: str, index: Index, prior=None, bigram=None,
                    limit: int = 9):
    """整串码拼不出来时退到最长有效前缀。返回 (结果, 没用上的尾巴)。

    与 src/Decoder.cpp 的 SearchLongestPrefix 同构。尾巴必须交给调用方 ——
    输入法那边要把它重新喂回去接着组字，吃掉就是丢字。
    """
    rows = decode(text, index, prior, bigram, limit)
    if rows or len(text) < 3:
        return rows, ""
    shortest = max(2, len(text) - MAX_FALLBACK_TAIL)
    for length in range(len(text) - 1, shortest - 1, -1):
        rows = decode(text[:length], index, prior, bigram, limit)
        if rows:
            return rows, text[length:]
    return [], ""


class Speller:
    """给一个词算出它的码，逐段。基准测试和造词都要用。"""

    def __init__(self, root: pathlib.Path = ROOT):
        hanzi = g.load_hanzi(root / "sources/hanzi-dictionary.txt")
        ids = g.load_ids(root / "sources/cjkvi-ids.txt")
        names = g.merge_names(g.load_names(root / "sources/component-names.yaml"),
                              g.load_gf0014())
        overrides = g.load_structure_overrides(
            root / "sources/structure-overrides.tsv")
        self.spec: dict[str, tuple[str, str, list[str]]] = {}
        best: dict[str, int] = {}
        for ch, pinyin, weight in g.load_charset(root / "sources/8105.dict.yaml"):
            if ch in best and best[ch] >= weight:
                continue
            best[ch] = weight
            src = (hanzi.get(ch) or {}).get("decomposition") or ids.get(ch, "")
            groups = [g.letters_for(p, names, hanzi)
                      for p in g.expand_components(ch, hanzi, ids, names)]
            groups = [sorted(s)[0] for s in groups if s] or [pinyin[0]]
            self.spec[ch] = (pinyin,
                             g.classify_structure(g.parse_ids(src), ch, overrides),
                             groups)

    def code(self, word: str, levels: int = 1) -> str | None:
        pys, sts, cols = [], [], [[] for _ in range(levels)]
        for ch in word:
            row = self.spec.get(ch)
            if row is None or not row[2]:
                return None
            pys.append(row[0])
            sts.append(row[1])
            for level in range(levels):
                # 只有一个部件的字，第二轮把第一个再写一遍。列必须对齐，
                # 否则第 i 位说的是哪个字就讲不清；「没有就重写」是个机械
                # 规则，不需要打字的人做任何判断。
                cols[level].append(row[2][min(level, len(row[2]) - 1)])
        return "".join(pys) + "".join(sts) + "".join("".join(c) for c in cols)


def bench(index: Index, phrases: dict[str, int], bigram, levels: int,
          use_prior: bool, sample: int) -> None:
    speller = Speller()
    prior = phrases if use_prior else {}
    hit1 = hit3 = total = 0.0
    skipped = 0
    elapsed = 0.0
    bylen: dict[int, list[float]] = collections.defaultdict(lambda: [0.0, 0.0])
    items = list(phrases.items())[:sample]
    for word, freq in items:
        code = speller.code(word, levels)
        if code is None:
            skipped += 1
            continue
        if any(ch not in speller.spec for ch in word):
            skipped += 1
            continue
        # 不看词库时，连这个词自己对二元组的贡献也要扣掉，否则等于拿答案
        # 去给答案打分。扣掉之后才是「词库里真的没有这个词」的样子。
        restore = []
        if not use_prior:
            value = math.log(freq + 1)
            for a, b in zip(word, word[1:]):
                key = a + b
                restore.append((key, value))
                bigram[key] = bigram.get(key, 0.0) - value
        started = time.perf_counter()
        rows = decode(code, index, prior, bigram, limit=3)
        elapsed += time.perf_counter() - started
        for key, value in restore:
            bigram[key] = bigram.get(key, 0.0) + value
        total += freq
        got = [w for w, _, _ in rows]
        first = bool(got) and got[0] == word
        hit1 += freq if first else 0.0
        hit3 += freq if word in got else 0.0
        bylen[len(word)][0] += freq if first else 0.0
        bylen[len(word)][1] += freq
    tag = "带词库排序" if use_prior else "纯拼(不看词库)"
    print(f"\n{tag}，每字 {levels} 个部件，样本 {len(items)} 条（跳过 {skipped}）")
    print(f"  首选 {hit1/total*100:.2f}%   前三 {hit3/total*100:.2f}%"
          f"   平均 {elapsed/max(len(items)-skipped,1)*1000:.2f} ms/次")
    for length in sorted(bylen):
        a, b = bylen[length]
        if b and length <= 6:
            print(f"    {length} 字词 首选 {a/b*100:.2f}%（占词频 {b/total*100:.1f}%）")


# 自检用例：(码, 不看词库时应排第一的结果)。挑的都是会踩到某条具体规则
# 的例子，任何一条变红都说明列式解码的某个前提被改坏了。
SELFTEST = [
    ("qingzs", "清"),          # n=1 退化成现有单字码
    ("yingbg", "应"),          # 与应物共用的梯级，必须还在
    ("rud", "入"),             # 无部件字：只有拼音＋结构（入是独体字）
    ("rudr", "入"),            # 无部件字的部件位用自己名字的首字母
    # 0.3.0 起 耀 也落在 yaozw 上（它的部件展开之后多了一个 w 打头的叫法），
    # 而 耀 比 瑶 常用得多，所以两列码的首选换了人。这不是退化，是「部件码
    # 穷尽」的代价：别条路多了，每条路上的字也就多了。要钉死 瑶，得打到
    # 它自己唯一的那条码 suyaoszcwbf（下一行）。
    ("suyaoszcw", "苏耀"),
    # 两字，每字两个部件。0.3.0 部件上限提到三位后，by 不再是瑶的部件码
    # 组合（瑶的部件码展开里没有 b 打头的两位组合），正确的三列码是 bf。
    ("suyaoszcwbf", "苏瑶"),
    ("zuxiasdk", "足下"),   # 下 是独体字（GF 0013-2009），结构位 d
    ("yangzhipengzszmsp", "杨志鹏"),      # 三字，人名，词库里没有
    ("yangzhipengzszmspyxn", "杨志鹏"),
    ("xuancibz", "选词"),      # 「码打得满」压过「字多」：选词 > 选此
]

# 死码兜底：整串拼不出来时退到最长有效前缀，尾巴要如实报出来。
FALLBACK_SELFTEST = [
    # 0.2.0 里这是死码：并入 GF 0014 之后「一」多了「横」这个名称，于是
    # 「下」有了 xiadh，整串码解得通了，不再是死码。换成 zuxiasdkq ——
    # 下的部件只有 一(横/一) 和 卜，没有 q 打头的，所以尾巴是 q。
    ("zuxiasdkq", "足下", "q"),
    ("henmazzrm", "很吗", "rm"),
    ("qqqqq", None, ""),       # 怎么退都拼不出来，就该老实交白卷
]


def selftest(index: Index, bigram) -> int:
    bad = 0
    for code, want in SELFTEST:
        rows = decode(code, index, {}, bigram, limit=3)
        got = [w for w, _, _ in rows]
        ok = bool(got) and got[0] == want
        bad += 0 if ok else 1
        print(f"  {'通过' if ok else '失败'}  {code:<22} -> "
              f"{'/'.join(got[:3]) or '（无解）'}  期望 {want}")
    # 结构位只认 zsbpd，别的字母必须解不出东西来，否则分段规则是漏的。
    for code in ("suyaoxx", "suyaozz9"):
        rows = decode(code, index, {}, bigram, limit=3)
        two = [w for w, _, _ in rows if len(w) == 2]
        ok = not two
        bad += 0 if ok else 1
        print(f"  {'通过' if ok else '失败'}  {code:<22} -> 应拒绝，实得 {two[:3]}")
    for code, want, want_tail in FALLBACK_SELFTEST:
        rows, tail = decode_fallback(code, index, {}, bigram, limit=3)
        got = [w for w, _, _ in rows]
        first = got[0] if got else None
        ok = first == want and tail == want_tail
        bad += 0 if ok else 1
        print(f"  {'通过' if ok else '失败'}  {code:<22} -> "
              f"{'/'.join(got[:3]) or '（无解）'} 尾巴 {tail or '（无）'}  "
              f"期望 {want or '（无解）'} 尾巴 {want_tail or '（无）'}")
    total = len(SELFTEST) + 2 + len(FALLBACK_SELFTEST)
    print(f"\n自检 {total} 项，失败 {bad} 项")
    return 1 if bad else 0


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):
        pass
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("code", nargs="?")
    ap.add_argument("--no-prior", action="store_true",
                    help="不加载词库，纯拼")
    ap.add_argument("--bench", action="store_true", help="跑 20 万词基准")
    ap.add_argument("--selftest", action="store_true", help="跑固定用例自检")
    ap.add_argument("--sample", type=int, default=20000)
    ap.add_argument("--limit", type=int, default=9)
    args = ap.parse_args()

    index = load_index()
    phrases = load_phrases(ROOT / "sources/base.dict.yaml")
    bigram = bigrams_from(phrases)
    prior = {} if args.no_prior else phrases

    if args.selftest:
        return selftest(index, bigram)

    if args.bench:
        for levels in (1, 2):
            for use_prior in (False, True):
                bench(index, phrases, bigram, levels, use_prior, args.sample)
        return 0

    if not args.code:
        ap.error("给一个码，或者加 --bench")
    started = time.perf_counter()
    rows = decode(args.code, index, prior, bigram, args.limit)
    elapsed = (time.perf_counter() - started) * 1000
    print(f"{args.code}  ({elapsed:.1f} ms)")
    for i, (word, _, in_dict) in enumerate(rows, 1):
        print(f"  {i}. {word}{'  [词库]' if in_dict else ''}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
