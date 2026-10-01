#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Decoder.h"
#include "rime_api.h"

namespace zuxia {

struct Candidate {
  std::wstring text;
  std::wstring comment;
  std::wstring label;
};

struct EngineSnapshot {
  bool handled = false;
  bool composing = false;
  std::wstring commit;
  std::wstring preedit;
  std::vector<Candidate> candidates;
  int highlighted = 0;
  int page_no = 0;
  bool last_page = true;
  // 组字串里已经选定（部分上屏前）的那一段有多长，字节数。>0 说明用户先选了
  // 前面一段，剩下的码不再是一整串码。
  int selection_start = 0;
};

class RimeEngine {
 public:
  RimeEngine();
  ~RimeEngine();

  RimeEngine(const RimeEngine&) = delete;
  RimeEngine& operator=(const RimeEngine&) = delete;

  bool Initialize(HMODULE module);
  void Shutdown();
  bool Ready() const;
  bool IsComposing() const;

  // Western (ASCII) input mode. While it is on the text service stays out of
  // the way and lets every key reach the application unchanged.
  // Rime 此刻收下的那串原始按键。组字窗里的 preedit 是装饰过的，「哪几个
  // 键真的进去了」只有这个说得准 —— 自检和排查按键丢失要用。
  std::string RawInput() const;

  bool IsAsciiMode() const;
  void SetAsciiMode(bool ascii);
  bool ToggleAsciiMode();
  EngineSnapshot ProcessKey(int keycode, int modifiers = 0);
  EngineSnapshot Snapshot();
  void Clear();

  static int VirtualKeyToRimeKey(WPARAM virtual_key);

  // The printable ASCII character a key produces under the current keyboard
  // layout and modifier state, or 0. Punctuation has no fixed virtual-key
  // mapping -- `?` is Shift+VK_OEM_2 on a US layout and elsewhere on others --
  // so it is resolved through the layout rather than tabulated.
  static int AsciiForKey(WPARAM virtual_key);
  static bool IsPunctuationKey(WPARAM virtual_key);

 private:
  EngineSnapshot ReadSnapshot(bool handled);
  // 列式解码器。两种排法（详见 RimeEngine.cpp 的 FillDecodedCandidates）：
  //   补位 —— Rime 的候选原样排在前面，解码器填这一页剩下的空位；
  //   带头 —— 打的是列式码而 Rime 的首选不合列（连打成句造出来的「蓝刖」），
  //           解码结果排到最前，Rime 留后面几位。
  // 数据仍是懒加载：第一次真的要解码时才读那三兆。
  static bool EnsureDecoder();
  void FillDecodedCandidates(EngineSnapshot* out);
  void ResetOverlay();
  // 落下解码器的第 index 个词：记进回流表、清掉组字，死码兜底时把没用上的
  // 尾巴按键喂回 Rime 接着组字。
  EngineSnapshot CommitOverlay(size_t index);
  // Fills candidate.comment with the candidate's own complete Zuxia code.
  static void AnnotateCode(const std::wstring& typed, Candidate* candidate);
  static void LoadCodeHints(const std::wstring& data_dir);
  bool InitializeRuntime(HMODULE module);
  static std::wstring ModuleDirectory(HMODULE module);
  static std::wstring LocalAppDataDirectory();

  RimeApi* api_ = nullptr;
  RimeSessionId session_ = 0;
  bool initialized_ = false;

  static std::mutex runtime_mutex_;
  static bool runtime_tried_;
  static unsigned long runtime_last_try_;
  static bool runtime_ready_;
  static HMODULE runtime_module_;
  static RimeApi* runtime_api_;
  static std::string shared_data_utf8_;
  static std::string user_data_utf8_;
  // Character -> its complete code(s). A character with several readings keeps
  // one code per reading, e.g. 行 -> {hangzx, hengzx, xingzx}.
  static std::unordered_map<std::wstring, std::vector<std::wstring>>
      code_hints_;
  static std::wstring shared_data_dir_;
  static ColumnarDecoder decoder_;
  static bool decoder_ready_;
  static std::mutex decoder_mutex_;
  static bool decoder_tried_;
  static unsigned long decoder_last_try_;
  // 上一帧交出去的解码候选。它们不在 Rime 眼里，所以数字选择键必须由
  // 这里截下来自己处理。
  std::vector<std::wstring> overlay_;
  // 合并之后这一页每个位置上是谁：overlay >= 0 是 overlay_ 的下标，
  // rime >= 0 是 Rime 这一页上的下标。只有页上真有解码候选时才非空 ——
  // 空着的时候所有按键原样交给 Rime。
  struct Slot {
    int rime = -1;
    int overlay = -1;
  };
  std::vector<Slot> slots_;
  // 解码结果排在了 Rime 前面（序号与 Rime 的对不上，选 Rime 的候选要换算）。
  bool decoder_leads_ = false;
  // 合并后的这一页由这里管高亮：上下键在这里截下。
  int highlight_ = 0;
  // 上一次交出去的那一页。上下键只换高亮，不重算候选。
  EngineSnapshot shown_;
  // 产生 overlay_ 的那串码，选中时要连同选中的字一起回流。
  std::string overlay_code_;
  // 死码兜底时没用上的那几位按键。选中兜底候选之后必须把它们重新喂回
  // Rime，否则用户打的码被我们吃掉了 —— 那是丢字，不是容错。
  std::string overlay_tail_;
};

}  // namespace zuxia
