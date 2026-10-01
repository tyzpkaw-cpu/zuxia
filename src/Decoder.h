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
// 它在 RimeEngine 里是**补位**用的：Rime 的候选原样排在前面，这个解码器填
// 候选页剩下的空位。（早先的规则是「Rime 一个候选都给不出来时才上场」，
// 那个门槛定错了：Rime 给出候选不等于给对。）数据仍是懒加载的 —— 第一次
// 真要解码时才读那三兆，没必要让每个宿主进程一上来就吃掉。
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
  //
  // 整串码拼不出来时会退到最长有效前缀（见 .cpp 里的 SearchLongestPrefix）。
  // 这时候 *fallback_tail 收下没用上的那几位按键，调用方必须把它们重新交回
  // 输入法；不给这个参数就等于放弃兜底结果的完整性，别那样用。
  //
  // 不是 const：会顺手看一眼用户表有没有被别的宿主进程追加过。
  std::vector<std::wstring> Decode(const std::string& keys, size_t limit,
                                   std::string* fallback_tail = nullptr);

  // 回流。用户从解码候选里选中过什么，就记在这张小表里，下次同一串码把它
  // 前置。精确命中，不动 beam search 的顺序 —— 打满档位的确定性不能被学习
  // 打乱。排序只看最近用过，不计次数：错选一次的代价就是下次选对，一步翻
  // 回来；而按次数排的话得再选好多次才追得上。
  //
  // 表在 %LOCALAPPDATA%\Zuxia\zuxia.decoder.user.tsv，纯文本、只追加，用户
  // 随时能用记事本打开看，删掉就等于忘掉全部。
  //
  // 隐私：这个文件与诊断日志相反 —— 它记录用户打了什么、选了什么。它只留
  // 在本机，永远不写进 zuxia.log，不参与任何上传。加任何新的写入点之前请
  // 先读 src/Diagnostics.h 顶上的那段约定。
  void SetUserTable(const std::wstring& path);

  // 记下一次选择。失败就是学不到，绝不影响这一次输入。
  void RecordChoice(const std::string& keys, const std::wstring& text);

 private:
  struct Cell {
    std::string syllable;
    char structure = 0;  // 0 表示这一位没给结构码
    char first = 0;      // 0 表示没给部件
    char second = 0;
    char third = 0;
  };

  const std::vector<char32_t>* Lookup(const Cell& cell) const;
  // 纯 beam search，不掺用户表。Decode 在它外面套一层前置。
  std::vector<std::wstring> Search(const std::string& keys,
                                   size_t limit) const;
  // 死码兜底：退到最长有效前缀，把没用上的尾巴放进 *tail。
  std::vector<std::wstring> SearchLongestPrefix(const std::string& keys,
                                                size_t limit,
                                                std::string* tail) const;
  void MaybeReloadUserTable();

  std::unordered_map<std::string, std::vector<char32_t>> codes_;
  std::unordered_map<char32_t, double> log_weight_;
  // 键是两个码位拼成的 64 位：高 32 位是前一个字，低 32 位是后一个。
  std::unordered_map<uint64_t, float> bigram_;
  std::unordered_map<std::string, bool> syllables_;
  size_t max_syllable_ = 0;
  bool ready_ = false;

  // 用户表。只追加，所以「长度没变」就等于「内容没变」，靠这个判断要不要
  // 重读 —— 于是在别的程序里打过的词，切过来第一次打就已经排在前面。
  std::wstring user_path_;
  long long user_bytes_ = -1;
  std::unordered_map<std::string, std::vector<std::wstring>> user_;
};

}  // namespace zuxia
