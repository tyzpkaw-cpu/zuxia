#!/usr/bin/env python3
"""生成 data/lua/zuxia_lunar_data.lua —— 「nl 出农历」用的历表。

农历和节气不能靠一个简单公式算：月的大小、闰哪个月、节气落在哪一天，都由
天文计算决定。这里用 sxtwl（寿星天文历的 C++ 实现，许剑伟）把 1900–2100
年逐日算一遍，压成两张表交给 data/lua/zuxia_lunar.lua：

  lunar  每个农历年一行：「正月初一的儒略日数 闰月(0 表示没有) 各月大小」，
         各月大小按先后排，含闰月，1 = 大月 30 天，0 = 小月 29 天。
  terms  每个公历年一行：24 个两位数的日期，从小寒到冬至，北京时间。

生成之后逐日自检：用表推出来的农历日期、节气和 sxtwl 直接算的必须一天不差，
否则不写文件。运行时不需要 sxtwl，生成的 .lua 进 git；改范围或换算法才需要
重跑：

    pip install sxtwl
    python3 data-tools/generate_lunar.py
"""
from __future__ import annotations

import argparse
import datetime
import pathlib
import sys

try:
    import sxtwl
except ImportError:  # pragma: no cover - only the maintainer runs this
    raise SystemExit("需要 sxtwl：pip install sxtwl")

FIRST_YEAR = 1900
LAST_YEAR = 2100


def jdn(day: datetime.date) -> int:
    """公历日期的儒略日数（整数，正午起算的那一天）。"""
    return day.toordinal() + 1721425


def build():
    # 逐日扫，记下每个农历月的起点。
    start = datetime.date(FIRST_YEAR, 1, 1)
    stop = datetime.date(LAST_YEAR + 1, 3, 1)
    months = []  # (jdn, lunar_year, month, is_leap)
    day = start
    one = datetime.timedelta(days=1)
    while day < stop:
        info = sxtwl.fromSolar(day.year, day.month, day.day)
        if info.getLunarDay() == 1:
            months.append((jdn(day), info.getLunarYear(), info.getLunarMonth(),
                           bool(info.isLunarLeap())))
        day += one

    lunar_rows = []
    for year in range(FIRST_YEAR, LAST_YEAR + 1):
        own = [i for i, m in enumerate(months) if m[1] == year]
        if not own or months[own[0]][2] != 1 or months[own[0]][3]:
            raise SystemExit(f"{year}: 找不到正月初一")
        sizes = []
        leap = 0
        for i in own:
            if i + 1 >= len(months):
                raise SystemExit(f"{year}: 扫描范围不够，最后一个月没有终点")
            length = months[i + 1][0] - months[i][0]
            if length not in (29, 30):
                raise SystemExit(f"{year}: 月长 {length}")
            sizes.append("1" if length == 30 else "0")
            if months[i][3]:
                leap = months[i][2]
        if len(sizes) != (13 if leap else 12):
            raise SystemExit(f"{year}: {len(sizes)} 个月，闰 {leap}")
        lunar_rows.append((months[own[0]][0], leap, "".join(sizes)))

    term_rows = []
    by_year = {}
    for year in range(FIRST_YEAR - 1, LAST_YEAR + 2):
        for item in sxtwl.getJieQiByYear(year):
            t = sxtwl.JD2DD(item.jd)
            by_year.setdefault(int(t.Y), {})[item.jqIndex] = (int(t.M), int(t.D))
    for year in range(FIRST_YEAR, LAST_YEAR + 1):
        found = by_year.get(year, {})
        days = []
        # 小寒(1) 大寒(2) 立春(3) … 大雪(23) 冬至(0)
        for k in list(range(1, 24)) + [0]:
            if k not in found:
                raise SystemExit(f"{year}: 缺节气 {k}")
            month, dom = found[k]
            want_month = (k + 1) // 2 if k else 12
            if month != want_month:
                raise SystemExit(f"{year}: 节气 {k} 落在 {month} 月")
            days.append(f"{dom:02d}")
        term_rows.append("".join(days))
    return lunar_rows, term_rows


def verify(lunar_rows, term_rows) -> int:
    """用压好的表逐日反推，与 sxtwl 一天一天对。返回核对过的天数。"""
    starts = [r[0] for r in lunar_rows]
    checked = 0
    day = datetime.date(FIRST_YEAR, 1, 31)
    end = datetime.date(LAST_YEAR, 12, 31)
    one = datetime.timedelta(days=1)
    while day <= end:
        j = jdn(day)
        info = sxtwl.fromSolar(day.year, day.month, day.day)
        # 表里的农历
        idx = max(i for i, s in enumerate(starts) if s <= j)
        ny, leap, sizes = lunar_rows[idx]
        offset = j - ny
        pos = 0
        while offset >= (30 if sizes[pos] == "1" else 29):
            offset -= 30 if sizes[pos] == "1" else 29
            pos += 1
        if leap and pos == leap:
            month, is_leap = leap, True
        elif leap and pos > leap:
            month, is_leap = pos, False
        else:
            month, is_leap = pos + 1, False
        got = (FIRST_YEAR + idx, month, offset + 1, is_leap)
        want = (info.getLunarYear(), info.getLunarMonth(), info.getLunarDay(),
                bool(info.isLunarLeap()))
        if got != want:
            raise SystemExit(f"{day}: 表推出 {got}，sxtwl 是 {want}")
        # 表里的节气
        row = term_rows[day.year - FIRST_YEAR]
        k1 = 2 * day.month - 1  # 这个月的第一个节气（1 起算）
        first = int(row[(k1 - 1) * 2:(k1 - 1) * 2 + 2])
        second = int(row[k1 * 2:k1 * 2 + 2])
        mine = None
        if day.day == first:
            mine = k1
        elif day.day == second:
            mine = k1 + 1
        theirs = None
        if info.hasJieQi():
            q = info.getJieQi()
            theirs = 24 if q == 0 else q
        if mine != theirs:
            raise SystemExit(f"{day}: 表里节气 {mine}，sxtwl 是 {theirs}")
        checked += 1
        day += one
    return checked


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=pathlib.Path,
                    default=root / "data/lua/zuxia_lunar_data.lua")
    args = ap.parse_args()

    lunar_rows, term_rows = build()
    days = verify(lunar_rows, term_rows)

    lines = [
        "-- Generated by data-tools/generate_lunar.py from sxtwl - do not edit.",
        "-- 农历与节气表，%d–%d 年，逐日与 sxtwl（寿星天文历）核对过 %d 天。"
        % (FIRST_YEAR, LAST_YEAR, days),
        "-- lunar：每个农历年一行，「正月初一的儒略日数 闰月(0=无) 各月大小」，",
        "--        各月大小按先后排（含闰月），1 = 大月 30 天，0 = 小月 29 天。",
        "-- terms：每个公历年一行，24 个两位数日期，从小寒到冬至（北京时间）。",
        "return {",
        "  first_year = %d," % FIRST_YEAR,
        "  lunar = {",
    ]
    for year, (ny, leap, sizes) in zip(range(FIRST_YEAR, LAST_YEAR + 1), lunar_rows):
        lines.append('    "%d %d %s", -- %d' % (ny, leap, sizes, year))
    lines.append("  },")
    lines.append("  terms = {")
    for year, row in zip(range(FIRST_YEAR, LAST_YEAR + 1), term_rows):
        lines.append('    "%s", -- %d' % (row, year))
    lines.append("  },")
    lines.append("}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {args.out} ({days} days verified)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
