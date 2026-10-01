-- 足下输入法：打 dt（或 dt:）出当前的日期和时间。
--
-- dt 在足下的码表里是死前缀（没有哪个音节以 dt 开头），拿来做触发码不会
-- 挡住任何字词；与雾凇拼音（rime-ice）的约定一致。方案里由 recognizer /
-- matcher 把整串 dt 认成一个段，所以这里的候选不会和码表的候选混在一起。
--
-- 只用 os.date("*t") 取数，自己拼字符串，不经 strftime：Windows 的 C 运行库
-- 对 %z、%A 之类的转换按系统区域给出本地化文本，结果因机器而异。

local DIGITS = { [0] = "〇", "一", "二", "三", "四", "五", "六", "七", "八", "九" }
local WEEKDAYS = { "日", "一", "二", "三", "四", "五", "六" }  -- os.date 的 wday：1 = 星期日

-- 年份逐位写：2026 -> 二〇二六
local function chinese_year(year)
  local out = {}
  for ch in tostring(year):gmatch("%d") do
    out[#out + 1] = DIGITS[tonumber(ch)]
  end
  return table.concat(out)
end

-- 月、日按读法写：1 -> 一，10 -> 十，15 -> 十五，21 -> 二十一，30 -> 三十
local function chinese_number(n)
  if n < 10 then return DIGITS[n] end
  local tens, ones = math.floor(n / 10), n % 10
  local head = (tens == 1) and "十" or (DIGITS[tens] .. "十")
  if ones == 0 then return head end
  return head .. DIGITS[ones]
end

-- 本地时间与 UTC 的差，形如 +08:00。不用 %z：见文件开头。
local function utc_offset(now)
  local here = os.date("*t", now)
  local utc = os.date("!*t", now)
  utc.isdst = here.isdst
  local seconds = os.difftime(now, os.time(utc))
  local sign = "+"
  if seconds < 0 then
    sign = "-"
    seconds = -seconds
  end
  local minutes = math.floor(seconds / 60 + 0.5)
  return string.format("%s%02d:%02d", sign, math.floor(minutes / 60), minutes % 60)
end

local function candidates(now)
  local t = os.date("*t", now)
  local y, m, d = t.year, t.month, t.day
  local week = "星期" .. WEEKDAYS[t.wday]
  local ymd = string.format("%d年%d月%d日", y, m, d)
  local iso_date = string.format("%04d-%02d-%02d", y, m, d)
  local hm = string.format("%02d:%02d", t.hour, t.min)
  local hms = string.format("%02d:%02d:%02d", t.hour, t.min, t.sec)
  return {
    { ymd, "" },
    { iso_date, "" },
    { string.format("%d月%d日", m, d), "" },
    { ymd .. " " .. week, "" },
    { chinese_year(y) .. "年" .. chinese_number(m) .. "月" .. chinese_number(d) .. "日", "" },
    { hm, "" },
    { iso_date .. " " .. hms, "" },
    { week, "" },
    { string.format("%04d/%02d/%02d", y, m, d), "" },
    { hms, "" },
    { iso_date .. "T" .. hms .. utc_offset(now), "ISO 8601" },
  }
end

local function translator(input, seg, env)
  if input ~= "dt" and input ~= "dt:" then return end
  for _, item in ipairs(candidates(os.time())) do
    yield(Candidate("date", seg.start, seg._end, item[1], item[2]))
  end
end

-- 测试脚本（data-tools/test_lua.py）直接调用这一张表里的函数。
return { func = translator, candidates = candidates }
