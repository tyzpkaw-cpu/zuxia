#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
  // Fills candidate.comment with the candidate's own complete Zuxia code.
  static void AnnotateCode(const std::wstring& typed, Candidate* candidate);
  static void LoadCodeHints(const std::wstring& data_dir);
  bool InitializeRuntime(HMODULE module);
  static std::wstring ModuleDirectory(HMODULE module);
  static std::wstring LocalAppDataDirectory();

  RimeApi* api_ = nullptr;
  RimeSessionId session_ = 0;
  bool initialized_ = false;

  static std::once_flag runtime_once_;
  static bool runtime_ready_;
  static HMODULE runtime_module_;
  static RimeApi* runtime_api_;
  static std::string shared_data_utf8_;
  static std::string user_data_utf8_;
  // Character -> its complete code(s). A character with several readings keeps
  // one code per reading, e.g. 行 -> {hangzx, hengzx, xingzx}.
  static std::unordered_map<std::wstring, std::vector<std::wstring>>
      code_hints_;
};

}  // namespace zuxia
