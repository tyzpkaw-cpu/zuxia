#!/usr/bin/env python3
"""核对 data/lua 下的 Lua 脚本：nl 出农历、dt 出日期时间。

    pip install lupa sxtwl
    python3 data-tools/test_lua.py

农历：1900-01-31 到 2100-12-31 逐日把 zuxia_lunar.lua 算出来的东西和 sxtwl
（寿星天文历）直接算的对一遍 —— 农历年月日、闰月、农历年干支、生肖、年柱、
月柱、日柱、今日节气、下一节气、节日，外加候选的条数和头一条的写法。
日期时间：固定一个时刻，在几个时区下核对 dt 的全部候选（含 UTC 偏移）。
最后按 librime-lua 的调法（协程 + yield）把两个翻译器各跑一遍，确认 dt、dt:、
nl、nl: 出候选，别的输入一条也不出。

全部通过返回 0；有一条不对就打印出来并返回 1。
"""
from __future__ import annotations

import datetime
import os
import pathlib
import sys
import time

try:
    import lupa
    try:
        from lupa import lua54 as lua_impl  # librime-lua 用的是 Lua 5.4
    except ImportError:  # pragma: no cover - 老版本 lupa 只有一个运行时
        lua_impl = lupa
except ImportError:
    raise SystemExit("需要 lupa：pip install lupa")
try:
    import sxtwl
except ImportError:
    raise SystemExit("需要 sxtwl：pip install sxtwl")

ROOT = pathlib.Path(__file__).resolve().parent.parent
LUA_DIR = ROOT / "data" / "lua"

STEMS = "甲乙丙丁戊己庚辛壬癸"
BRANCHES = "子丑寅卯辰巳午未申酉戌亥"
ZODIAC = "鼠牛虎兔龙蛇马羊猴鸡狗猪"
MONTHS = ["正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"]
DIGITS = "〇一二三四五六七八九"
TERMS = ["小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
         "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
         "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"]
FESTIVALS = {(1, 1): "春节", (1, 15): "元宵节", (2, 2): "龙抬头", (5, 5): "端午节",
             (7, 7): "七夕", (7, 15): "中元节", (8, 15): "中秋节", (9, 9): "重阳节",
             (12, 8): "腊八节", (12, 23): "小年"}

FIRST = datetime.date(1900, 1, 31)  # 1900 年正月初一
LAST = datetime.date(2100, 12, 31)

# sxtwl 自己前后不一的两天。这两个「节」都落在北京时间刚过子夜（1917 年大雪
# 12 月 8 日 00:00:59，1927 年白露 9 月 9 日 00:05:24）：sxtwl 的 hasJieQi 和
# 我们的节气表都说节在后一天，它的 getMonthGZ 却提前一天换了月柱 —— 寿星历对
# 1929 年以前用北京地方时（东经 116°23′，比东八区早约 14 分钟）排历。这里按
# 节气表走（节当天起换月柱），这两天不拿 sxtwl 的月柱比。只涉及 1929 年以前，
# nl 只出今天的农历，碰不到。
SXTWL_MONTH_GZ_PRE_1929 = {datetime.date(1917, 12, 7), datetime.date(1927, 9, 8)}

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)
    if len(failures) <= 30:
        print("FAIL", message)


def check(cond: bool, message: str) -> None:
    if not cond:
        fail(message)


def new_lua():
    lua = lua_impl.LuaRuntime(unpack_returned_tuples=True)
    lua.globals().package.path = LUA_DIR.as_posix() + "/?.lua;" + lua.globals().package.path
    # librime-lua 的约定：翻译器在协程里跑，yield 就是 coroutine.yield；
    # Candidate(类型, 起, 止, 文字, 注释) 由 C++ 提供，这里用一张表代替。
    lua.execute("""
        yield = coroutine.yield
        function Candidate(kind, s, e, text, comment)
          return { type = kind, start = s, _end = e, text = text, comment = comment }
        end
        function collect(func, input)
          local seg = { start = 0, _end = #input }
          local co = coroutine.create(function() func(input, seg, {}) end)
          local out = {}
          while true do
            local ok, cand = coroutine.resume(co)
            if not ok then error(cand) end
            if coroutine.status(co) == "dead" then break end
            out[#out + 1] = cand
          end
          return out
        end
    """)
    return lua


def lua_list(table) -> list:
    return [table[i] for i in range(1, len(table) + 1)]


def gz(obj) -> str:
    return STEMS[obj.tg] + BRANCHES[obj.dz]


def day_name(d: int) -> str:
    if d == 10:
        return "初十"
    if d == 20:
        return "二十"
    if d == 30:
        return "三十"
    return "初十廿"[d // 10] + DIGITS[d % 10]


def test_lunar(lua) -> int:
    mod = lua.eval('(require("zuxia_lunar"))')
    info, candidates = mod.info, mod.candidates

    # 先把 sxtwl 的结果逐日算好（多算一个月，给「下一节气」和「除夕」用）
    one = datetime.timedelta(days=1)
    days = []
    day = FIRST
    while day <= LAST + datetime.timedelta(days=40):
        days.append((day, sxtwl.fromSolar(day.year, day.month, day.day)))
        day += one
    terms_after = [None] * len(days)  # 每天之后（不含当天）的第一个节气
    upcoming = None
    for i in range(len(days) - 1, -1, -1):
        terms_after[i] = upcoming
        d, s = days[i]
        if s.hasJieQi():
            q = s.getJieQi()
            upcoming = (d, TERMS[23 if q == 0 else q - 1])

    checked = 0
    for i, (d, s) in enumerate(days):
        if d > LAST:
            break
        where = d.isoformat()
        it = info(d.year, d.month, d.day)
        if it is None:
            fail(f"{where}: info 返回 nil")
            continue
        lunar = it["lunar"]
        want = (s.getLunarYear(), s.getLunarMonth(), s.getLunarDay(), bool(s.isLunarLeap()))
        got = (lunar["year"], lunar["month"], lunar["day"], bool(lunar["leap"]))
        check(got == want, f"{where}: 农历 {got}，sxtwl {want}")

        check(it["year_gz"] == gz(s.getYearGZ(True)), f"{where}: 农历年 {it['year_gz']}")
        check(it["zodiac"] == ZODIAC[(want[0] - 4) % 12], f"{where}: 生肖 {it['zodiac']}")
        check(it["pillar_year"] == gz(s.getYearGZ()), f"{where}: 年柱 {it['pillar_year']}，sxtwl {gz(s.getYearGZ())}")
        if d not in SXTWL_MONTH_GZ_PRE_1929:
            check(it["pillar_month"] == gz(s.getMonthGZ()), f"{where}: 月柱 {it['pillar_month']}，sxtwl {gz(s.getMonthGZ())}")
        check(it["pillar_day"] == gz(s.getDayGZ()), f"{where}: 日柱 {it['pillar_day']}，sxtwl {gz(s.getDayGZ())}")

        term = None
        if s.hasJieQi():
            q = s.getJieQi()
            term = TERMS[23 if q == 0 else q - 1]
        check(it["today_term"] == term, f"{where}: 今日节气 {it['today_term']}，sxtwl {term}")

        nxt = terms_after[i]
        if nxt is not None and nxt[0].year > LAST.year:
            nxt = None  # 表只到 2100 年
        got_next = None
        if it["next_term"] is not None:
            n = it["next_term"]
            got_next = (n["month"], n["day"], n["name"])
        want_next = None if nxt is None else (nxt[0].month, nxt[0].day, nxt[1])
        check(got_next == want_next, f"{where}: 下一节气 {got_next}，sxtwl {want_next}")

        tomorrow = days[i + 1][1]
        if (tomorrow.getLunarMonth(), tomorrow.getLunarDay(), tomorrow.isLunarLeap()) == (1, 1, False):
            festival = "除夕"
        elif s.isLunarLeap():
            festival = None
        else:
            festival = FESTIVALS.get((want[1], want[2]))
        check(it["festival"] == festival, f"{where}: 节日 {it['festival']}，应为 {festival}")

        cands = lua_list(candidates(d.year, d.month, d.day))
        md = ("闰" if want[3] else "") + MONTHS[want[1] - 1] + "月" + day_name(want[2])
        expect_count = 6 + (festival is not None) + (term is not None) + (want_next is not None)
        check(len(cands) == expect_count, f"{where}: {len(cands)} 条候选，应为 {expect_count}")
        check(len(cands) > 0 and cands[0][1] == md, f"{where}: 头一条 {cands[0][1] if cands else None}，应为 {md}")
        checked += 1

    # 几个看得见的例子，写死整张候选表
    def texts(y, m, d):
        return [c[1] for c in lua_list(candidates(y, m, d))]

    golden = {
        (2026, 10, 1): ["八月廿一", "农历八月廿一", "丙午年八月廿一", "丙午马年八月廿一",
                        "农历丙午年八月廿一", "丙午年 丁酉月 戊申日", "10月8日寒露"],
        (2025, 10, 6): ["八月十五", "农历八月十五", "乙巳年八月十五", "乙巳蛇年八月十五",
                        "农历乙巳年八月十五", "中秋节", "乙巳年 乙酉月 戊申日", "10月8日寒露"],
        (2023, 3, 22): ["闰二月初一", "农历闰二月初一", "癸卯年闰二月初一", "癸卯兔年闰二月初一",
                        "农历癸卯年闰二月初一", "癸卯年 乙卯月 己卯日", "4月5日清明"],
    }
    for (y, m, d), want in golden.items():
        got = texts(y, m, d)
        check(got == want, f"{y}-{m:02d}-{d:02d}: 候选 {got}\n    应为 {want}")
    eve = texts(2026, 2, 16)
    check("除夕" in eve, f"2026-02-16 应有「除夕」：{eve}")
    check(lua_list(candidates(1900, 1, 30)) == [], "1900-01-30 在表外，应无候选")
    check(lua_list(candidates(2101, 1, 1)) == [], "2101-01-01 在表外，应无候选")
    return checked


def test_datetime() -> int:
    # 2026-10-01 14:05:09（星期四），在几个时区下各核对一遍
    expect = ["2026年10月1日", "2026-10-01", "10月1日", "2026年10月1日 星期四",
              "二〇二六年十月一日", "14:05", "2026-10-01 14:05:09", "星期四",
              "2026/10/01", "14:05:09"]
    zones = {"Asia/Shanghai": "+08:00", "UTC": "+00:00",
             "America/New_York": "-04:00", "Asia/Kolkata": "+05:30"}
    checked = 0
    saved = os.environ.get("TZ")
    try:
        for zone, offset in zones.items():
            if not pathlib.Path("/usr/share/zoneinfo", zone).exists() and zone != "UTC":
                print(f"skip {zone}: 本机没有时区数据")
                continue
            os.environ["TZ"] = zone
            time.tzset()
            lua = new_lua()
            mod = lua.eval('(require("zuxia_datetime"))')
            now = lua.eval("os.time{year=2026, month=10, day=1, hour=14, min=5, sec=9}")
            got = [c[1] for c in lua_list(mod.candidates(now))]
            want = expect + ["2026-10-01T14:05:09" + offset]
            check(got == want, f"dt @ {zone}: {got}\n    应为 {want}")
            checked += 1
    finally:
        if saved is None:
            os.environ.pop("TZ", None)
        else:
            os.environ["TZ"] = saved
        time.tzset()

    # 汉字月日：1–31 全部
    lua = new_lua()
    mod = lua.eval('(require("zuxia_datetime"))')
    for m, d in [(1, 1), (2, 10), (3, 15), (11, 20), (12, 21), (12, 30), (12, 31)]:
        now = lua.eval(f"os.time{{year=2000, month={m}, day={d}, hour=12}}")
        got = lua_list(mod.candidates(now))[4][1]
        def cn(n):
            if n < 10:
                return DIGITS[n]
            return ("" if n // 10 == 1 else DIGITS[n // 10]) + "十" + (DIGITS[n % 10] if n % 10 else "")
        want = "二〇〇〇年" + cn(m) + "月" + cn(d) + "日"
        check(got == want, f"汉字日期 {m}/{d}: {got}，应为 {want}")
    return checked


def test_translators() -> None:
    lua = new_lua()
    collect = lua.globals().collect
    for name, hits, misses in [
        ("zuxia_datetime", ["dt", "dt:"], ["d", "dtx", "dt::", "nl", ""]),
        ("zuxia_lunar", ["nl", "nl:"], ["n", "nlx", "nl::", "dt", ""]),
    ]:
        mod = lua.eval(f'(require("{name}"))')
        for text in hits:
            out = lua_list(collect(mod.func, text))
            check(len(out) >= 7, f"{name}({text!r}) 只出了 {len(out)} 条候选")
            for c in out:
                check(c["start"] == 0 and c["_end"] == len(text), f"{name}({text!r}) 候选范围不对")
                check(isinstance(c["text"], str) and c["text"] != "", f"{name}({text!r}) 有空候选")
            if out:
                print(f"{name}({text!r}): {out[0]['text']} …（{len(out)} 条）")
        for text in misses:
            out = lua_list(collect(mod.func, text))
            check(len(out) == 0, f"{name}({text!r}) 不该出候选，出了 {len(out)} 条")


def main() -> int:
    lua = new_lua()
    print("Lua", lua.eval("_VERSION"))
    days = test_lunar(lua)
    print(f"农历：逐日核对 {days} 天")
    zones = test_datetime()
    print(f"日期时间：{zones} 个时区")
    test_translators()
    if failures:
        print(f"{len(failures)} 处不对")
        return 1
    print("全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
