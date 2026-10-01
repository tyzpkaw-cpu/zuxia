#include "Decoder.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace zuxia {
namespace {

// 一个字最多给两个部件，与单字方案的梯级一致。
// 一个字最多带几个部件字母。0.2.0 是 2；作者实测反馈「部件码还没有穷尽」
// 「需要再补一次部件码」，0.3.0 放宽到 3。
// data-tools/generate_zuxia.py 的 MAX_COMPONENTS_PER_CODE 必须和它一致，
// audit_zuxia.py 有一条断言盯着。
constexpr size_t kMaxComponents = 3;
// 一个词最多这么多字。再长的串切法数量会爆，而且也没人那么打。
constexpr size_t kMaxSyllables = 12;
// 定向搜索宽度。逐位扩展时只留权重最高的这么多个前缀。
constexpr size_t kBeam = 400;
// 同一串码的切法数上限。按「字多的在前」排过序，靠后的基本都是废解。
constexpr size_t kMaxSegmentations = 64;
// 二元组权重。1.0 时「连得上」和「字本身常见」同等重要；实测 1.0 最好。
constexpr double kBigramWeight = 1.0;

bool IsStructureKey(char key) {
  return key == 'z' || key == 's' || key == 'b' || key == 'p' || key == 'd';
}

// UTF-8 一个码位。走到坏字节就退回把这个字节当码位，不抛异常 —— 这是在
// 宿主进程里跑的代码，宁可解出个怪字也不能把 Word 拖下水。
char32_t NextCodePoint(const std::string& text, size_t* at) {
  const unsigned char lead = static_cast<unsigned char>(text[*at]);
  size_t extra = 0;
  char32_t value = lead;
  if (lead >= 0xF0) {
    extra = 3;
    value = lead & 0x07u;
  } else if (lead >= 0xE0) {
    extra = 2;
    value = lead & 0x0Fu;
  } else if (lead >= 0xC0) {
    extra = 1;
    value = lead & 0x1Fu;
  }
  if (*at + extra >= text.size()) extra = 0;
  for (size_t i = 0; i < extra; ++i) {
    const unsigned char next = static_cast<unsigned char>(text[*at + 1 + i]);
    if ((next & 0xC0u) != 0x80u) {
      extra = 0;
      value = lead;
      break;
    }
    value = (value << 6) | (next & 0x3Fu);
  }
  *at += extra + 1;
  return value;
}

std::vector<char32_t> Utf8ToCodePoints(const std::string& text) {
  std::vector<char32_t> out;
  size_t at = 0;
  while (at < text.size()) out.push_back(NextCodePoint(text, &at));
  return out;
}

std::wstring CodePointsToWide(const std::u32string& text) {
  std::wstring out;
  for (char32_t point : text) {
    if (point >= 0x10000 && point <= 0x10FFFF) {
      const char32_t offset = point - 0x10000;
      out.push_back(static_cast<wchar_t>(0xD800 + (offset >> 10)));
      out.push_back(static_cast<wchar_t>(0xDC00 + (offset & 0x3FF)));
    } else {
      out.push_back(static_cast<wchar_t>(point));
    }
  }
  return out;
}

bool ReadWholeFile(const std::wstring& path, std::string* out,
                   unsigned long* error) {
  if (error) *error = 0;
  // FILE_SHARE_WRITE 必须给：回流表同时被别的宿主进程以 FILE_APPEND_DATA
  // 打开着，只声明 FILE_SHARE_READ 会直接 ERROR_SHARING_VIOLATION ——
  // 表现是「回流偶尔整个失效」。FILE_SHARE_DELETE 让压缩时的改名能顶上去。
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (error) *error = GetLastError();
    return false;
  }
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      size.QuadPart > (64 << 20)) {
    // Empty or implausibly large means a broken install rather than an I/O
    // fault, and GetLastError would only report whatever came before.
    if (error) *error = ERROR_FILE_INVALID;
    CloseHandle(file);
    return false;
  }
  out->resize(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  const BOOL ok = ReadFile(file, out->data(),
                           static_cast<DWORD>(out->size()), &read, nullptr);
  // Read before CloseHandle: closing a handle can overwrite the thread's
  // last-error value.
  if (!ok && error) *error = GetLastError();
  CloseHandle(file);
  if (!ok) return false;
  out->resize(read);
  return true;
}

}  // namespace

namespace {

// 同一串码最多前置这么多个学过的结果。名额小是故意的：剩下的位置留给
// beam search，它的顺序一个字都不动。
constexpr size_t kUserRecall = 3;
// 用户表最多留这么多行，超了就把最旧的丢掉重写一遍。防的是文件无限长，
// 不是防误选 —— 误选靠「最近用过的在前」自己纠正。
constexpr size_t kUserMaxLines = 2000;

// 整串码拼不出来时，最多允许把末尾这么多位当成「还没打完／打错了」而退回去
// 重试。定 3 位是有依据的：一个字的码最多多出结构位 + 两个部件位，正好三位，
// 所以「少打一个字的尾巴」这件事一定落在 3 位以内。再往上退就不是兜底而是
// 猜了 —— 退五位六位之后剩下的那个词跟用户打的码已经没什么关系，端到候选
// 窗里只会干扰。顺带把兜底的代价钉死在 3 次 beam search 以内。
constexpr size_t kMaxFallbackTail = 3;

// 只认 a-z；分隔符吃掉；别的一概不认 —— 混进一个数字就当整串不是码，
// 悄悄抹掉它会让 suyaozz9 解成「诉呀哦」，那是无中生有。
std::string NormalizeKeys(const std::string& raw) {
  std::string keys;
  for (char ch : raw) {
    if (ch >= 'a' && ch <= 'z') {
      keys.push_back(ch);
    } else if (ch != '\'' && ch != ' ') {
      return std::string();
    }
  }
  return keys;
}

std::string WideToUtf8(const std::wstring& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    char32_t cp = static_cast<char32_t>(text[i]);
    // Windows 上 wchar_t 是 16 位，基本区之外的字是一对代理项。
    if constexpr (sizeof(wchar_t) == 2) {
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
        const char32_t low = static_cast<char32_t>(text[i + 1]);
        if (low >= 0xDC00 && low <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          ++i;
        }
      }
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

long long FileBytes(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return -1;
  LARGE_INTEGER size = {};
  const BOOL ok = GetFileSizeEx(file, &size);
  CloseHandle(file);
  return ok ? static_cast<long long>(size.QuadPart) : -1;
}

bool AppendLine(const std::wstring& path, const std::string& line) {
  // 只申请 FILE_APPEND_DATA。同时申请 FILE_WRITE_DATA 会让它退回普通写语义，
  // 而新句柄的文件指针在 0 —— 诊断日志就是这么把自己一行行覆写掉的。几个
  // 宿主进程共写这个文件，单次写远小于一个扇区，追加由文件系统串行化。
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  // WriteFile 返回 TRUE 也可能只写进去一部分（磁盘满、配额到顶）。半行留在
  // 文件里，下一次追加会跟它粘成一行，解析出来就是一个谁也没打过的码对着
  // 一个谁也没选过的词 —— 而回流表的东西是排在候选第一位的。所以要么把
  // 这一行补完，要么至少补一个换行把它隔断，并且如实返回失败。
  size_t done = 0;
  bool ok = true;
  while (done < line.size()) {
    DWORD written = 0;
    // WriteFile 返回 FALSE 时 written 仍然是真的写进去了多少，所以先记账
    // 再判成败 —— 不然补换行那一步会以为一个字节都没写出去。
    const BOOL wrote = WriteFile(file, line.data() + done,
                                 static_cast<DWORD>(line.size() - done),
                                 &written, nullptr);
    done += written;
    if (!wrote || written == 0) {
      ok = false;
      break;
    }
  }
  if (!ok && done > 0 && line[done - 1] != '\n') {
    // 已经写进去半行了，补个换行封住它。补不上也只能这样。
    DWORD ignored = 0;
    WriteFile(file, "\n", 1, &ignored, nullptr);
  }
  CloseHandle(file);
  return ok;
}

bool WriteAllBytes(HANDLE file, const std::string& bytes) {
  const char* cursor = bytes.data();
  size_t left = bytes.size();
  while (left > 0) {
    DWORD written = 0;
    if (!WriteFile(file, cursor, static_cast<DWORD>(left), &written, nullptr) ||
        written == 0) {
      return false;
    }
    cursor += written;
    left -= written;
  }
  return FlushFileBuffers(file) != FALSE;
}

// 压缩回流表用。CREATE_ALWAYS 直接截原文件的话，另一个宿主进程正好在追加
// 就会写到被截掉的偏移上，中间留一段 NUL；写一半失败更是直接把用户攒下的
// 回流清空。所以先写临时文件，全部落盘了再改名顶上去。
bool ReplaceWholeFile(const std::wstring& path, const std::string& bytes) {
  const std::wstring temp = path + L".new";
  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const bool ok = WriteAllBytes(file, bytes);
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(temp.c_str());
    return false;
  }
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    // 别的进程正抓着这个文件。这一轮不压缩，下一轮再来。
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

}  // namespace

bool ColumnarDecoder::Load(const std::wstring& path, unsigned long* error) {
  ready_ = false;
  std::string text;
  if (!ReadWholeFile(path, &text, error)) return false;

  size_t begin = 0;
  while (begin < text.size()) {
    size_t end = text.find('\n', begin);
    if (end == std::string::npos) end = text.size();
    size_t stop = end;
    if (stop > begin && text[stop - 1] == '\r') --stop;
    const std::string line = text.substr(begin, stop - begin);
    begin = end + 1;
    if (line.size() < 3 || line[0] == '#' || line[1] != '\t') continue;

    const char tag = line[0];
    const size_t second = line.find('\t', 2);
    const std::string field =
        line.substr(2, second == std::string::npos ? std::string::npos
                                                   : second - 2);
    const std::string rest =
        second == std::string::npos ? std::string() : line.substr(second + 1);

    if (tag == 's') {
      size_t at = 0;
      while (at < field.size()) {
        size_t space = field.find(' ', at);
        if (space == std::string::npos) space = field.size();
        if (space > at) {
          const std::string syllable = field.substr(at, space - at);
          syllables_[syllable] = true;
          max_syllable_ = (std::max)(max_syllable_, syllable.size());
        }
        at = space + 1;
      }
    } else if (tag == 'w') {
      const std::vector<char32_t> points = Utf8ToCodePoints(field);
      if (points.size() != 1) continue;
      const double weight = atof(rest.c_str());
      log_weight_[points[0]] = std::log(weight > 1.0 ? weight : 1.0);
    } else if (tag == 'c') {
      codes_[field] = Utf8ToCodePoints(rest);
    } else if (tag == 'b') {
      const std::vector<char32_t> points = Utf8ToCodePoints(field);
      if (points.size() != 2) continue;
      // 数据文件被手工改成 nan/inf 的话，这个权重会一路传进 :490 的排序
      // 比较器，「小于」就不再满足严格弱序，std::sort 可能越界写。拦在这儿。
      // 上面 'w' 那支不用管：NaN 走 `weight > 1.0` 的 false 分支，取 log(1)=0。
      double weight_value = atof(rest.c_str());
      if (!std::isfinite(weight_value)) weight_value = 0.0;
      const uint64_t key = (static_cast<uint64_t>(points[0]) << 32) | points[1];
      bigram_[key] = static_cast<float>(weight_value);
    }
  }

  // 每个码下的字按字频排好。定向搜索截断时留下的就是常见的那几个，
  // 顺序也因此是确定的 —— 不然同一串码两次调用可能给出不同的次序。
  for (auto& entry : codes_) {
    std::sort(entry.second.begin(), entry.second.end(),
              [this](char32_t a, char32_t b) {
                const auto x = log_weight_.find(a);
                const auto y = log_weight_.find(b);
                const double wa = x == log_weight_.end() ? 0.0 : x->second;
                const double wb = y == log_weight_.end() ? 0.0 : y->second;
                if (wa != wb) return wa > wb;
                return a < b;
              });
  }
  ready_ = !syllables_.empty() && !codes_.empty() && !log_weight_.empty();
  return ready_;
}

const std::vector<char32_t>* ColumnarDecoder::Lookup(const Cell& cell) const {
  std::string code = cell.syllable;
  if (cell.structure) {
    code.push_back(cell.structure);
    // 部件段排序之后再查。
    //
    // 「任意部件、顺序不限」意味着部件段是个多重集，不是序列 —— abc 和 cba
    // 是同一个码。表里只存排好序的那一个代表（见 generate_phrases.py），
    // 所以这里要先排。存排列的话 c 段是 2.66 MB，存组合只有 0.6 MB，而首次
    // 按键要等的正是这个文件读完（实测 1.4–1.8 秒）。
    char letters[kMaxComponents] = {0};
    size_t count = 0;
    if (cell.first) letters[count++] = cell.first;
    if (cell.second) letters[count++] = cell.second;
    if (cell.third) letters[count++] = cell.third;
    std::sort(letters, letters + count);
    code.append(letters, count);
  }
  const auto found = codes_.find(code);
  return found == codes_.end() ? nullptr : &found->second;
}

// 1) 把码切成「拼音音节 + 尾巴」。尾巴长度必须落在 [0, 4 × 字数] 内（结构
//    一位 + 部件最多三位），否则这串码不可能是这么多个字 —— 这条约束就是
//    列式码之所以可判定的原因。返回的切法已按 Search 的优先次序排好。
std::vector<ColumnarDecoder::Split> ColumnarDecoder::SplitKeys(
    const std::string& keys) const {
  std::vector<Split> splits;
  std::vector<std::string> stack;
  // 显式栈的递归：位置 + 下一个要试的音节长度。
  struct Frame {
    size_t pos;
    size_t size;
  };
  std::vector<Frame> frames;
  frames.push_back({0, 0});
  while (!frames.empty()) {
    Frame& top = frames.back();
    if (top.size == 0 && !stack.empty()) {
      const std::string tail = keys.substr(top.pos);
      if (tail.size() <= (kMaxComponents + 1) * stack.size()) {
        splits.push_back({stack, tail});
      }
    }
    ++top.size;
    const size_t room = keys.size() - top.pos;
    const size_t widest = (std::min)(max_syllable_, room);
    bool advanced = false;
    while (top.size <= widest) {
      const std::string head = keys.substr(top.pos, top.size);
      if (syllables_.count(head) && stack.size() < kMaxSyllables) {
        const size_t next = top.pos + top.size;
        stack.push_back(head);
        frames.push_back({next, 0});
        advanced = true;
        break;
      }
      ++top.size;
    }
    if (advanced) continue;
    frames.pop_back();
    if (!stack.empty()) stack.pop_back();
  }
  if (splits.empty()) return splits;

  std::stable_sort(splits.begin(), splits.end(),
                   [](const Split& a, const Split& b) {
                     const int ca = ColumnsFilled(a);
                     const int cb = ColumnsFilled(b);
                     if (ca != cb) return ca > cb;
                     if (a.syllables.size() != b.syllables.size()) {
                       return a.syllables.size() > b.syllables.size();
                     }
                     return a.tail.size() < b.tail.size();
                   });
  if (splits.size() > kMaxSegmentations) splits.resize(kMaxSegmentations);
  return splits;
}

// 一个切法「把码用满了几列」：尾巴是逐列左对齐填的，一列宽度等于音节数。
// 码表出的永远是整列（足下 = zuxia / zuxiasd / zuxiasdky，没有 zuxias），
// 所以尾巴长度正好是列宽的整数倍，才说明这个切法和使用者打的是同一件事。
//
// 判据从「字多」换成这个，是因为字多会把 xuancibz 判错：xu|an|ci 三个字
// 只填得上 2 个结构位（半列），却胜过 xuan|ci —— 后者两个结构位填满，
// 正是「选词」。打得满的那个切法才是使用者的本意，字数多少是次要的。
int ColumnarDecoder::ColumnsFilled(const Split& split) {
  const size_t m = split.syllables.size();
  if (m == 0) return -1;
  if (split.tail.size() % m != 0) return -1;  // 半列：码表不会出这种码
  return static_cast<int>(split.tail.size() / m);
}

// 尾巴按列分派：tail[i] 是第 i 个字的结构位，tail[(level+1)·n + i] 是它的
// 第 level+1 个部件。结构位只认 zsbpd；没给结构却给了部件是不合法的 ——
// 三段是左对齐逐位填的。
bool ColumnarDecoder::AssignColumns(const Split& split,
                                    std::vector<Cell>* cells) {
  const size_t n = split.syllables.size();
  cells->assign(n, Cell());
  for (size_t i = 0; i < n; ++i) {
    Cell& cell = (*cells)[i];
    cell.syllable = split.syllables[i];
    if (i < split.tail.size()) {
      const char key = split.tail[i];
      if (!IsStructureKey(key)) return false;
      cell.structure = key;
    }
    for (size_t level = 0; level < kMaxComponents; ++level) {
      const size_t at = (level + 1) * n + i;
      if (at >= split.tail.size()) break;
      if (!cell.structure) return false;
      if (level == 0) {
        cell.first = split.tail[at];
      } else if (level == 1) {
        cell.second = split.tail[at];
      } else {
        cell.third = split.tail[at];
      }
    }
  }
  return true;
}

bool ColumnarDecoder::Fits(const std::string& raw,
                           const std::wstring& word) const {
  if (!ready_ || word.empty()) return false;
  const std::string keys = NormalizeKeys(raw);
  if (keys.size() < 2) return false;
  std::u32string target;
  for (size_t i = 0; i < word.size(); ++i) {
    char32_t ch = static_cast<char32_t>(word[i]);
    if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < word.size()) {
      const char32_t low = static_cast<char32_t>(word[i + 1]);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        ch = 0x10000 + ((ch - 0xD800) << 10) + (low - 0xDC00);
        ++i;
      }
    }
    target.push_back(ch);
  }
  if (target.size() > kMaxSyllables) return false;
  std::vector<Cell> cells;
  for (const Split& split : SplitKeys(keys)) {
    if (split.syllables.size() != target.size()) continue;
    if (!AssignColumns(split, &cells)) continue;
    bool all = true;
    for (size_t i = 0; i < cells.size() && all; ++i) {
      const std::vector<char32_t>* chars = Lookup(cells[i]);
      all = chars != nullptr &&
            std::find(chars->begin(), chars->end(), target[i]) != chars->end();
    }
    if (all) return true;
  }
  return false;
}

std::vector<std::wstring> ColumnarDecoder::Search(const std::string& raw,
                                                  size_t limit,
                                                  int* top_columns) const {
  std::vector<std::wstring> result;
  if (top_columns) *top_columns = -1;
  if (!ready_ || limit == 0) return result;

  const std::string keys = NormalizeKeys(raw);
  if (keys.size() < 2) return result;

  const std::vector<Split> splits = SplitKeys(keys);
  if (splits.empty()) return result;

  // 2) 尾巴按列分派，逐位查表，定向搜索。
  //
  // 节点是定宽的：一个词最多 kMaxSyllables 个字，所以 word 用定长数组而不是
  // std::u32string。差别不小 —— 九个音节的纯拼串要扩 14 万个节点，每个都
  // 堆分配一次的话要 34 ms，够打字时看得见卡顿；改成定宽后是 3 ms。
  struct Beam {
    char32_t word[kMaxSyllables];
    size_t length = 0;
    double score = 0.0;
  };
  // 每个词记下「它是从填得多满的切法来的」和「词本身的分」。前者是主判据：
  // 半列切法解出来的词一律排在整列切法之后，而不是和它们按分数混在一起 ——
  // 分数是词频，比不过「使用者到底指定了什么」。
  struct Ranked {
    int columns = -1;
    double value = 0.0;
    bool Beats(const Ranked& other) const {
      if (columns != other.columns) return columns > other.columns;
      return value > other.value;
    }
  };
  std::unordered_map<std::u32string, Ranked> best;
  std::vector<Cell> cells;
  for (const Split& split : splits) {
    const int columns = ColumnsFilled(split);
    if (!AssignColumns(split, &cells)) continue;

    // 纯拼那一档 Rime 自己的连打成句管着，这里只是兜底，窄一点就够；
    // 带形码的串每位只剩一两个字，宽窄都无所谓。
    const size_t width = split.tail.empty() ? 48 : kBeam;
    std::vector<Beam> beam(1);
    std::vector<Beam> next;
    for (const Cell& cell : cells) {
      const std::vector<char32_t>* chars = Lookup(cell);
      if (!chars || chars->empty()) {
        beam.clear();
        break;
      }
      next.clear();
      next.reserve(beam.size() * chars->size());
      for (const Beam& one : beam) {
        if (one.length >= kMaxSyllables) continue;
        for (char32_t ch : *chars) {
          const auto weight = log_weight_.find(ch);
          double step = weight == log_weight_.end() ? 0.0 : weight->second;
          if (one.length) {
            const uint64_t key =
                (static_cast<uint64_t>(one.word[one.length - 1]) << 32) | ch;
            const auto link = bigram_.find(key);
            if (link != bigram_.end()) step += kBigramWeight * link->second;
          }
          Beam grown = one;
          grown.word[grown.length++] = ch;
          grown.score = one.score + step;
          next.push_back(grown);
        }
      }
      if (next.size() > width) {
        std::partial_sort(next.begin(), next.begin() + width, next.end(),
                          [](const Beam& a, const Beam& b) {
                            return a.score > b.score;
                          });
        next.resize(width);
      }
      beam = next;
    }
    for (const Beam& one : beam) {
      if (one.length == 0) continue;
      // 按字数归一，否则长词的分永远比短词高，两种切法就没法比。
      const double value = one.score / static_cast<double>(one.length);
      const std::u32string word(one.word, one.length);
      const Ranked scored{columns, value};
      const auto seen = best.find(word);
      if (seen == best.end()) {
        best.emplace(word, scored);
      } else if (scored.Beats(seen->second)) {
        seen->second = scored;
      }
    }
  }

  std::vector<std::pair<std::u32string, Ranked>> ranked(best.begin(),
                                                        best.end());
  std::sort(ranked.begin(), ranked.end(),
            [](const std::pair<std::u32string, Ranked>& a,
               const std::pair<std::u32string, Ranked>& b) {
              if (a.second.columns != b.second.columns) {
                return a.second.columns > b.second.columns;
              }
              if (a.second.value != b.second.value) {
                return a.second.value > b.second.value;
              }
              return a.first < b.first;  // 平分时定序，免得两次调用顺序不同
            });
  if (top_columns && !ranked.empty()) *top_columns = ranked.front().second.columns;
  for (const auto& one : ranked) {
    if (result.size() >= limit) break;
    result.push_back(CodePointsToWide(one.first));
  }
  return result;
}

void ColumnarDecoder::SetUserTable(const std::wstring& path) {
  user_path_ = path;
  user_bytes_ = -1;  // 下一次解码时读一遍
  user_.clear();
}

void ColumnarDecoder::MaybeReloadUserTable() {
  if (user_path_.empty()) return;
  const long long bytes = FileBytes(user_path_);
  if (bytes == user_bytes_) return;  // 只追加的文件，长度没变就是没变
  if (bytes <= 0) {
    user_bytes_ = bytes;
    user_.clear();
    return;
  }

  std::string text;
  // 读失败（别人正独占着、瞬时 I/O 错）时不能记下这个长度 —— 记了就再也
  // 不会重读，回流表在这个进程里等于永久失效。原样退出，下一次再试。
  if (!ReadWholeFile(user_path_, &text, nullptr)) return;
  user_bytes_ = bytes;
  user_.clear();

  // 文件不是以换行收尾 —— 最后那一行是半写的，扔掉。
  if (!text.empty() && text.back() != '\n') {
    const size_t last = text.find_last_of('\n');
    text.resize(last == std::string::npos ? 0 : last + 1);
  }

  size_t lines = 0;
  size_t begin = 0;
  while (begin < text.size()) {
    size_t end = text.find('\n', begin);
    if (end == std::string::npos) end = text.size();
    size_t stop = end;
    if (stop > begin && text[stop - 1] == '\r') --stop;
    const std::string line = text.substr(begin, stop - begin);
    begin = end + 1;
    ++lines;

    const size_t tab = line.find('\t');
    if (tab == 0 || tab == std::string::npos) continue;
    const size_t next = line.find('\t', tab + 1);
    const size_t word_end = (next == std::string::npos) ? line.size() : next;
    const std::string code = NormalizeKeys(line.substr(0, tab));
    if (code.size() < 2) continue;
    const std::vector<char32_t> points =
        Utf8ToCodePoints(line.substr(tab + 1, word_end - tab - 1));
    if (points.empty()) continue;
    const std::wstring word =
        CodePointsToWide(std::u32string(points.begin(), points.end()));
    if (word.empty()) continue;

    // 文件是按时间先后追加的，所以从头读到尾、每条都插到最前面，最后得到
    // 的就是「最近用过的在前」。同一条重复出现只挪位置，不占新名额。
    std::vector<std::wstring>& learned = user_[code];
    learned.erase(std::remove(learned.begin(), learned.end(), word),
                  learned.end());
    learned.insert(learned.begin(), word);
    if (learned.size() > kUserRecall) learned.resize(kUserRecall);
  }

  if (lines <= kUserMaxLines) return;
  // 超了就把最旧的那些丢掉。内存里那份不用动 —— 每串码本来只留 kUserRecall
  // 条，丢掉的行只影响下一次重读。
  size_t drop = lines - kUserMaxLines;
  size_t at = 0;
  while (drop > 0) {
    const size_t end = text.find('\n', at);
    if (end == std::string::npos) return;
    at = end + 1;
    --drop;
  }
  if (at < text.size() && ReplaceWholeFile(user_path_, text.substr(at))) {
    user_bytes_ = FileBytes(user_path_);
  }
}

void ColumnarDecoder::RecordChoice(const std::string& raw,
                                   const std::wstring& text) {
  if (user_path_.empty() || text.empty()) return;
  const std::string code = NormalizeKeys(raw);
  if (code.size() < 2) return;

  std::string line = code;
  line += '\t';
  line += WideToUtf8(text);
  line += '\n';
  if (!AppendLine(user_path_, line)) return;

  // 本进程立刻生效，不等下一次重读。同时把记下的长度对上，免得白读一遍。
  std::vector<std::wstring>& learned = user_[code];
  learned.erase(std::remove(learned.begin(), learned.end(), text),
                learned.end());
  learned.insert(learned.begin(), text);
  if (learned.size() > kUserRecall) learned.resize(kUserRecall);
  user_bytes_ = FileBytes(user_path_);
}

// 整串码一个词都拼不出来的时候，退到最长的拼得出来的前缀。
//
// 为什么需要它：列式码是定长的，打到一半的码几乎总是拼不出东西 —— 打
// henmazzrm，如果 rm 那两位打错了（或者那个字还没打完），整串就是死码，
// 解码器一声不吭。这不是纠错，是别在用户打字的半途中途突然变哑。
//
// 只往回退 kMaxFallbackTail 位，并且把没用上的那几位从 *tail 交出去 ——
// 调用方必须把它们重新喂回输入法，否则用户打的码就被我们悄悄吃掉了。
// 这一点是硬要求，不是建议：少了它，兜底就从「帮一把」变成「丢字」。
std::vector<std::wstring> ColumnarDecoder::SearchLongestPrefix(
    const std::string& raw, size_t limit, std::string* tail) const {
  std::vector<std::wstring> result;
  if (!ready_ || limit == 0) return result;

  const std::string keys = NormalizeKeys(raw);
  // 前缀自己至少得有 2 位（Search 的下限），所以码不够长就没得退。
  if (keys.size() < 3) return result;
  const size_t shortest =
      keys.size() > kMaxFallbackTail ? keys.size() - kMaxFallbackTail : 2;
  for (size_t length = keys.size() - 1; length >= shortest; --length) {
    if (length < 2) break;
    const std::string prefix = keys.substr(0, length);
    result = Search(prefix, limit);
    if (result.empty()) continue;
    // 回流也得跟着退。选中兜底候选时记下的是这段前缀（见
    // RimeEngine::FillDecodedCandidates 里的 overlay_code_），所以查也必须
    // 按前缀查 —— 不然「学过」和「查得到」对不上，用户教过一次的词在同一
    // 串码上第二次还是不排前面。
    const auto learned = user_.find(prefix);
    if (learned != user_.end()) {
      size_t front = 0;
      for (const std::wstring& word : learned->second) {
        if (front >= kUserRecall) break;
        auto at = std::find(result.begin(), result.end(), word);
        if (at != result.end()) result.erase(at);
        result.insert(result.begin() + front, word);
        ++front;
      }
      if (result.size() > limit) result.resize(limit);
    }
    if (tail) *tail = keys.substr(length);
    return result;
  }
  result.clear();
  return result;
}

std::vector<std::wstring> ColumnarDecoder::Decode(const std::string& raw,
                                                  size_t limit,
                                                  std::string* fallback_tail,
                                                  int* columns) {
  if (fallback_tail) fallback_tail->clear();
  if (columns) *columns = -1;
  MaybeReloadUserTable();

  std::vector<std::wstring> result;
  if (limit == 0) return result;

  // 先看这串码本身解不解得出东西。回流表只管把用户选过的往前排，不该凭空
  // 造候选：数据升级（或用户手改过这张表）之后，一条早就解不出任何字的旧
  // 记录会把这串码伪装成「仍然有效」，于是兜底那段不跑、多出来的那几位按键
  // 也不会交回 Rime —— 用户真按过的键就凭空消失了。
  std::vector<std::wstring> exact = Search(raw, limit, columns);
  if (exact.empty()) {
    // 这串码整体是死码。退到最长有效前缀，并报出没用上的尾巴。
    return SearchLongestPrefix(raw, limit, fallback_tail);
  }

  const std::string code = NormalizeKeys(raw);
  if (code.size() >= 2) {
    const auto learned = user_.find(code);
    if (learned != user_.end()) {
      for (const std::wstring& word : learned->second) {
        if (result.size() >= kUserRecall || result.size() >= limit) break;
        result.push_back(word);
      }
    }
  }

  for (std::wstring& word : exact) {
    if (result.size() >= limit) break;
    if (std::find(result.begin(), result.end(), word) != result.end()) continue;
    result.push_back(std::move(word));
  }
  return result;
}

}  // namespace zuxia
