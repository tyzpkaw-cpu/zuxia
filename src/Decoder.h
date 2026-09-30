#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zuxia {

// 列式码解码器 —— 词库里没有的词也能拼出来。
//
// 一个词的码是三段拼接，逐位左对齐：
//
//     全拼串  +  逐位结构串  +  逐位部件串
//     suyao      sz            cw          -> 苏瑶
//
// 这与单字规则同构（n=1 时退化成 su + s + c），所以打字的人只记一套规则。
// 代价是 Rime 自带的 table_translator 切不动它：结构位和部件位与它们描述的
// 那个字并不相邻，必须按「列」而不是按「行」去配。这个类就是那个按列配的
// 解码器。算法与 data-tools/decode_zuxia.py 逐行对应，那边有 11 条自检。
//
// 只在 Rime 一个候选都给不出来的时候才上场，所以数据是懒加载的：多数人
// 打一天字也用不到它，没必要让每个宿主进程都先吃三兆内存。
class ColumnarDecoder {
 public:
  // data 目录下的 zuxia.decoder.tsv。失败返回 false，之后 Ready() 恒假。
  // On failure *error, when supplied, receives the Win32 error that stopped
  // the read. Without it "the table did not load" cannot be told apart from
  // "the file is not there", which are different faults with different fixes.
  bool Load(const std::wstring& path, unsigned long* error = nullptr);
  bool Ready() const { return ready_; }

  // keys 是已经打出的那串原始按键（只认 a-z）。返回最多 limit 个词，
  // 好的在前。拼不出来就返回空。
  std::vector<std::wstring> Decode(const std::string& keys,
                                   size_t limit) const;

 private:
  struct Cell {
    std::string syllable;
    char structure = 0;  // 0 表示这一位没给结构码
    char first = 0;      // 0 表示没给部件
    char second = 0;
  };

  const std::vector<char32_t>* Lookup(const Cell& cell) const;

  std::unordered_map<std::string, std::vector<char32_t>> codes_;
  std::unordered_map<char32_t, double> log_weight_;
  // 键是两个码位拼成的 64 位：高 32 位是前一个字，低 32 位是后一个。
  std::unordered_map<uint64_t, float> bigram_;
  std::unordered_map<std::string, bool> syllables_;
  size_t max_syllable_ = 0;
  bool ready_ = false;
};

}  // namespace zuxia
