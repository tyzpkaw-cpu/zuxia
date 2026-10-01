-- 足下输入法：打 nl（或 nl:）出今天的农历。
--
-- nl 在足下的码表里是死前缀（没有哪个音节以 nl 开头），拿来做触发码不会挡住
-- 任何字词。方案里由 recognizer / matcher 把整串 nl 认成一个段。
--
-- 农历的月大月小、闰月、节气日期都查 zuxia_lunar_data.lua（由
-- data-tools/generate_lunar.py 用 sxtwl 生成，1900–2100 年逐日核对过）；
-- 干支、生肖、节日由日期推算。表外的日子不出候选。
-- 日期取本机的本地时间；节气按北京时间算，跟国内日历一致。

local data = require("zuxia_lunar_data")

local STEMS = { "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸" }
local BRANCHES = { "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥" }
local ZODIAC = { "鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪" }
local MONTHS = { "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊" }
local DIGITS = { "一", "二", "三", "四", "五", "六", "七", "八", "九" }
local TERMS = {
  "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
  "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
  "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
}
-- 农历节日，键是 月×100+日；闰月不过节。除夕另算（腊月最后一天）。
local FESTIVALS = {
  [101] = "春节", [115] = "元宵节", [202] = "龙抬头", [505] = "端午节",
  [707] = "七夕", [715] = "中元节", [815] = "中秋节", [909] = "重阳节",
  [1208] = "腊八节", [1223] = "小年",
}

-- 公历日期的儒略日数（整数）。只用 math.floor，不用 //，各版本 Lua 都能跑。
local function jdn(y, m, d)
  local a = math.floor((14 - m) / 12)
  local yy = y + 4800 - a
  local mm = m + 12 * a - 3
  return d + math.floor((153 * mm + 2) / 5) + 365 * yy + math.floor(yy / 4)
    - math.floor(yy / 100) + math.floor(yy / 400) - 32045
end

-- 干支序号（0 = 甲子）转文字
local function ganzhi(n)
  n = n % 60
  return STEMS[n % 10 + 1] .. BRANCHES[n % 12 + 1]
end

local function day_name(d)
  if d == 10 then return "初十" end
  if d == 20 then return "二十" end
  if d == 30 then return "三十" end
  return ({ "初", "十", "廿" })[math.floor(d / 10) + 1] .. DIGITS[d % 10]
end

local function month_name(m, leap)
  return (leap and "闰" or "") .. MONTHS[m] .. "月"
end

-- 农历年表按需解析，解析过的留着
local rows = {}
local function lunar_row(i)
  local row = rows[i]
  if row == nil then
    local start, leap, sizes = data.lunar[i]:match("^(%d+) (%d+) ([01]+)$")
    row = { start = tonumber(start), leap = tonumber(leap), sizes = sizes }
    rows[i] = row
  end
  return row
end

-- 儒略日数 -> 农历 { year, month, day, leap, eve }；表外返回 nil。
-- eve 为真表示这一天是农历年的最后一天（除夕）。
local function lunar_of(j)
  local count = #data.lunar
  if count == 0 or j < lunar_row(1).start then return nil end
  local lo, hi = 1, count
  while lo < hi do
    local mid = math.floor((lo + hi + 1) / 2)
    if lunar_row(mid).start <= j then lo = mid else hi = mid - 1 end
  end
  local row = lunar_row(lo)
  local offset = j - row.start
  local pos, len = 1, 0
  while true do
    if pos > #row.sizes then return nil end  -- 过了表里最后一年
    len = (row.sizes:sub(pos, pos) == "1") and 30 or 29
    if offset < len then break end
    offset = offset - len
    pos = pos + 1
  end
  local month, leap = pos, false
  if row.leap > 0 then
    if pos == row.leap + 1 then
      month, leap = row.leap, true
    elseif pos > row.leap + 1 then
      month = pos - 1
    end
  end
  return {
    year = data.first_year + lo - 1,
    month = month,
    day = offset + 1,
    leap = leap,
    eve = (pos == #row.sizes and offset + 1 == len),
  }
end

-- 某公历年第 k 个节气（1 = 小寒 … 24 = 冬至）的日子；表外返回 nil。
-- 第 k 个节气落在第 ceil(k/2) 月。
local function term_day(y, k)
  local row = data.terms[y - data.first_year + 1]
  if row == nil then return nil end
  return tonumber(row:sub(2 * k - 1, 2 * k))
end

-- 一天的全部农历信息；表外返回 nil。测试脚本 data-tools/test_lua.py 逐日核对它。
local function info(y, m, d)
  local lunar = lunar_of(jdn(y, m, d))
  if lunar == nil or term_day(y, 1) == nil then return nil end

  -- 年柱以立春为界，月柱以「节」为界（小寒、立春、惊蛰 … 大雪，每月第一个节气）
  local spring = term_day(y, 3)
  local pillar_year = (m > 2 or (m == 2 and d >= spring)) and y or (y - 1)
  local solar_month = (d >= term_day(y, 2 * m - 1)) and m or (m - 1)
  local month_index = (y * 12 + solar_month - 1 + 13) % 60

  -- 今天的节气、下一个节气
  local today_term, next_term = nil, nil
  for k = 2 * m - 1, 24 do
    local tm = math.floor((k + 1) / 2)
    local td = term_day(y, k)
    if tm == m and td == d then
      today_term = TERMS[k]
    elseif tm > m or (tm == m and td > d) then
      next_term = { name = TERMS[k], month = tm, day = td }
      break
    end
  end
  if next_term == nil then
    local td = term_day(y + 1, 1)
    if td ~= nil then next_term = { name = TERMS[1], month = 1, day = td } end
  end

  local festival = nil
  if lunar.eve then
    festival = "除夕"
  elseif not lunar.leap then
    festival = FESTIVALS[lunar.month * 100 + lunar.day]
  end

  return {
    lunar = lunar,
    year_gz = ganzhi(lunar.year - 4),       -- 农历年的干支，以春节为界
    zodiac = ZODIAC[(lunar.year - 4) % 12 + 1],
    pillar_year = ganzhi(pillar_year - 4),  -- 下面三柱以节气为界
    pillar_month = ganzhi(month_index),
    pillar_day = ganzhi(jdn(y, m, d) + 49),
    festival = festival,
    today_term = today_term,
    next_term = next_term,
  }
end

-- 候选：{ 文字, 注释 } 的列表；表外返回空表。
local function candidates(y, m, d)
  local it = info(y, m, d)
  if it == nil then return {} end
  local md = month_name(it.lunar.month, it.lunar.leap) .. day_name(it.lunar.day)
  local out = {
    { md, "" },
    { "农历" .. md, "" },
    { it.year_gz .. "年" .. md, "" },
    { it.year_gz .. it.zodiac .. "年" .. md, "" },
    { "农历" .. it.year_gz .. "年" .. md, "" },
  }
  if it.festival then out[#out + 1] = { it.festival, "" } end
  if it.today_term then out[#out + 1] = { it.today_term, "今日节气" } end
  out[#out + 1] = {
    it.pillar_year .. "年 " .. it.pillar_month .. "月 " .. it.pillar_day .. "日", "干支",
  }
  if it.next_term then
    local n = it.next_term
    out[#out + 1] = { string.format("%d月%d日%s", n.month, n.day, n.name), "下一节气" }
  end
  return out
end

local function translator(input, seg, env)
  if input ~= "nl" and input ~= "nl:" then return end
  local t = os.date("*t")
  for _, item in ipairs(candidates(t.year, t.month, t.day)) do
    yield(Candidate("lunar", seg.start, seg._end, item[1], item[2]))
  end
end

return { func = translator, candidates = candidates, info = info, jdn = jdn }
