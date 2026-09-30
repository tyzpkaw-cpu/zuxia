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
constexpr size_t kMaxComponents = 2;
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
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
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
      const uint64_t key = (static_cast<uint64_t>(points[0]) << 32) | points[1];
      bigram_[key] = static_cast<float>(atof(rest.c_str()));
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
    if (cell.first) {
      code.push_back(cell.first);
      if (cell.second) code.push_back(cell.second);
    }
  }
  const auto found = codes_.find(code);
  return found == codes_.end() ? nullptr : &found->second;
}

std::vector<std::wstring> ColumnarDecoder::Decode(const std::string& raw,
                                                  size_t limit) const {
  std::vector<std::wstring> result;
  if (!ready_ || limit == 0) return result;

  std::string keys;
  for (char ch : raw) {
    if (ch >= 'a' && ch <= 'z') {
      keys.push_back(ch);
    } else if (ch != '\'' && ch != ' ') {
      // 分隔符可以吃掉，别的一概不认。混进一个数字就当整串不是码 ——
      // 悄悄抹掉它会让 suyaozz9 解成「诉呀哦」，那是无中生有。
      return result;
    }
  }
  if (keys.size() < 2) return result;

  // 1) 把前缀切成拼音音节。尾巴长度必须落在 [0, 3 × 字数] 内，否则这串码
  //    不可能是这么多个字 —— 这条约束就是列式码之所以可判定的原因。
  struct Split {
    std::vector<std::string> syllables;
    std::string tail;
  };
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
  if (splits.empty()) return result;

  // 字多的解排前面：同一串码若能解成更长的词，那通常就是本意。
  std::stable_sort(splits.begin(), splits.end(),
                   [](const Split& a, const Split& b) {
                     if (a.syllables.size() != b.syllables.size()) {
                       return a.syllables.size() > b.syllables.size();
                     }
                     return a.tail.size() < b.tail.size();
                   });
  if (splits.size() > kMaxSegmentations) splits.resize(kMaxSegmentations);

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
  std::unordered_map<std::u32string, double> best;
  for (const Split& split : splits) {
    const size_t n = split.syllables.size();
    std::vector<Cell> cells(n);
    bool usable = true;
    for (size_t i = 0; i < n && usable; ++i) {
      cells[i].syllable = split.syllables[i];
      if (i < split.tail.size()) {
        const char key = split.tail[i];
        if (!IsStructureKey(key)) {
          usable = false;
          break;
        }
        cells[i].structure = key;
      }
      for (size_t level = 0; level < kMaxComponents; ++level) {
        const size_t at = (level + 1) * n + i;
        if (at >= split.tail.size()) break;
        // 没给结构却给了部件是不合法的：三段是左对齐逐位填的。
        if (!cells[i].structure) {
          usable = false;
          break;
        }
        if (level == 0) {
          cells[i].first = split.tail[at];
        } else {
          cells[i].second = split.tail[at];
        }
      }
    }
    if (!usable) continue;

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
      const auto seen = best.find(word);
      if (seen == best.end()) {
        best.emplace(word, value);
      } else if (value > seen->second) {
        seen->second = value;
      }
    }
  }

  std::vector<std::pair<std::u32string, double>> ranked(best.begin(),
                                                        best.end());
  std::sort(ranked.begin(), ranked.end(),
            [](const std::pair<std::u32string, double>& a,
               const std::pair<std::u32string, double>& b) {
              if (a.second != b.second) return a.second > b.second;
              return a.first < b.first;  // 平分时定序，免得两次调用顺序不同
            });
  for (const auto& one : ranked) {
    if (result.size() >= limit) break;
    result.push_back(CodePointsToWide(one.first));
  }
  return result;
}

}  // namespace zuxia
