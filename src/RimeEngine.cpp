#include "RimeEngine.h"

#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <utility>

#include "Diagnostics.h"
#include "Utf.h"

namespace zuxia {

namespace {

constexpr int kXkBackSpace = 0xff08;
constexpr int kXkReturn = 0xff0d;
constexpr int kXkEscape = 0xff1b;
constexpr int kXkHome = 0xff50;
constexpr int kXkLeft = 0xff51;
constexpr int kXkUp = 0xff52;
constexpr int kXkRight = 0xff53;
constexpr int kXkDown = 0xff54;
constexpr int kXkPageUp = 0xff55;
constexpr int kXkPageDown = 0xff56;
constexpr int kXkEnd = 0xff57;
constexpr int kXkDelete = 0xffff;

}  // namespace

std::once_flag RimeEngine::runtime_once_;
bool RimeEngine::runtime_ready_ = false;
HMODULE RimeEngine::runtime_module_ = nullptr;
RimeApi* RimeEngine::runtime_api_ = nullptr;
std::string RimeEngine::shared_data_utf8_;
std::string RimeEngine::user_data_utf8_;
std::unordered_map<std::wstring, std::vector<std::wstring>>
    RimeEngine::code_hints_;

RimeEngine::RimeEngine() = default;

RimeEngine::~RimeEngine() { Shutdown(); }

bool RimeEngine::Initialize(HMODULE module) {
  if (Ready()) return true;
  std::call_once(runtime_once_, [this, module]() {
    runtime_ready_ = InitializeRuntime(module);
  });
  if (!runtime_ready_) return false;

  api_ = runtime_api_;
  if (!api_) return false;
  session_ = api_->create_session();
  if (!session_) return false;
  if (!api_->select_schema(session_, "zuxia")) {
    LogEvent(L"engine-failed", L"select_schema(zuxia) rejected");
    api_->destroy_session(session_);
    session_ = 0;
    return false;
  }
  api_->set_option(session_, "ascii_mode", False);
  initialized_ = true;
  return true;
}

void RimeEngine::Shutdown() {
  if (api_ && session_) {
    api_->destroy_session(session_);
  }
  session_ = 0;
  initialized_ = false;
}

bool RimeEngine::Ready() const {
  return initialized_ && api_ && session_ &&
         api_->find_session(session_);
}

bool RimeEngine::IsComposing() const {
  if (!Ready()) return false;
  RIME_STRUCT(RimeStatus, status);
  const bool ok = api_->get_status(session_, &status);
  const bool composing = ok && status.is_composing;
  if (ok) api_->free_status(&status);
  return composing;
}

EngineSnapshot RimeEngine::ProcessKey(int keycode, int modifiers) {
  if (!Ready()) return {};
  const bool handled = api_->process_key(session_, keycode, modifiers);
  return ReadSnapshot(handled);
}

EngineSnapshot RimeEngine::Snapshot() {
  if (!Ready()) return {};
  return ReadSnapshot(false);
}

void RimeEngine::Clear() {
  if (Ready()) api_->clear_composition(session_);
}

bool RimeEngine::IsAsciiMode() const {
  if (!initialized_ || !api_ || !session_) return false;
  return api_->get_option(session_, "ascii_mode") != False;
}

void RimeEngine::SetAsciiMode(bool ascii) {
  if (!Ready()) return;
  // Anything half-typed belongs to the mode being left.
  api_->clear_composition(session_);
  api_->set_option(session_, "ascii_mode", ascii ? True : False);
}

bool RimeEngine::ToggleAsciiMode() {
  const bool next = !IsAsciiMode();
  SetAsciiMode(next);
  return next;
}

// Reads the generated one-code-per-reading table. Failure is not fatal: the
// candidate window simply shows no code annotations.
void RimeEngine::LoadCodeHints(const std::wstring& data_dir) {
  code_hints_.clear();
  std::ifstream input(data_dir + L"\\zuxia_char_codes.dict.yaml",
                      std::ios::binary);
  if (!input) return;

  std::string line;
  bool started = false;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == 0x0D) line.pop_back();
    if (!started) {
      if (line.rfind("...", 0) == 0) started = true;
      continue;
    }
    if (line.empty() || line[0] == '#') continue;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) continue;
    const std::string text = line.substr(0, tab);
    std::string code = line.substr(tab + 1);
    const size_t next_tab = code.find('\t');
    if (next_tab != std::string::npos) code.erase(next_tab);
    if (code.empty()) continue;
    try {
      code_hints_[Utf8ToWide(text)].push_back(Utf8ToWide(code));
    } catch (...) {
      // Skip malformed rows rather than failing the whole table.
    }
  }
}

// `typed` is the code the user has entered so far. A character with several
// readings only shows the reading that matches it, so typing `hang` annotates
// 行 with `hangzx` alone instead of all three of its codes.
void RimeEngine::AnnotateCode(const std::wstring& typed, Candidate* candidate) {
  if (code_hints_.empty() || !candidate) return;
  const auto found = code_hints_.find(candidate->text);
  if (found == code_hints_.end()) return;

  // 足下 accepts any two components in either order, so one character can
  // carry many equally complete codes. Show the ones that continue what has
  // been typed and stop after three, leaving the row readable: a complete
  // code is now pinyin + structure + two component letters, about seven
  // characters wide, so four of them would no longer fit.
  constexpr size_t kMaxCodes = 3;
  std::wstring comment;
  size_t shown = 0;
  for (const std::wstring& code : found->second) {
    // Filter first, cap second. The other order appends the ellipsis as soon
    // as the loop reaches a code the typed prefix rules out, promising more
    // codes that do not exist: 行 has nine complete codes across three
    // readings, but under `hang` only three of them are reachable.
    if (!typed.empty() && code.rfind(typed, 0) != 0) continue;
    if (shown == kMaxCodes) {
      comment += L" \u2026";
      break;
    }
    if (!comment.empty()) comment += L' ';
    comment += code;
    ++shown;
  }
  if (shown == 0) {
    // Nothing matched -- a partial or mistyped code. Show the first readings.
    for (const std::wstring& code : found->second) {
      if (shown == kMaxCodes) {
        comment += L" \u2026";
        break;
      }
      if (!comment.empty()) comment += L' ';
      comment += code;
      ++shown;
    }
  }
  candidate->comment = comment;
}

EngineSnapshot RimeEngine::ReadSnapshot(bool handled) {
  EngineSnapshot output;
  output.handled = handled;

  RIME_STRUCT(RimeCommit, commit);
  if (api_->get_commit(session_, &commit)) {
    try {
      if (commit.text) output.commit = Utf8ToWide(commit.text);
    } catch (...) {
      output.commit.clear();
    }
    api_->free_commit(&commit);
  }

  RIME_STRUCT(RimeContext, context);
  if (!api_->get_context(session_, &context)) {
    return output;
  }

  output.composing = context.composition.length > 0;
  try {
    if (context.composition.preedit) {
      output.preedit = Utf8ToWide(context.composition.preedit);
    }
  } catch (...) {
    output.preedit.clear();
  }
  output.highlighted = context.menu.highlighted_candidate_index;
  output.page_no = context.menu.page_no;
  output.last_page = context.menu.is_last_page != False;

  for (int i = 0; i < context.menu.num_candidates; ++i) {
    Candidate candidate;
    const RimeCandidate& source = context.menu.candidates[i];
    try {
      if (source.text) candidate.text = Utf8ToWide(source.text);
      if (source.comment) candidate.comment = Utf8ToWide(source.comment);
      if (context.select_labels && context.select_labels[i]) {
        candidate.label = Utf8ToWide(context.select_labels[i]);
      } else if (context.menu.select_keys &&
                 i < static_cast<int>(strlen(context.menu.select_keys))) {
        candidate.label.assign(
            1, static_cast<wchar_t>(context.menu.select_keys[i]));
      } else {
        candidate.label = std::to_wstring(i + 1);
      }
    } catch (...) {
      candidate.label = std::to_wstring(i + 1);
    }
    // A character's own complete code is more useful here than librime's
    // completion hint, so it replaces the comment whenever one is known.
    AnnotateCode(output.preedit, &candidate);
    output.candidates.push_back(std::move(candidate));
  }

  api_->free_context(&context);
  return output;
}

bool RimeEngine::InitializeRuntime(HMODULE module) {
  std::filesystem::path module_dir(ModuleDirectory(module));
  LogEvent(L"engine-start", module_dir.wstring());
  const std::filesystem::path runtime_path = module_dir / L"rime.dll";
  if (!std::filesystem::exists(runtime_path)) {
    LogEvent(L"engine-failed", L"rime.dll not found beside the text service");
    return false;
  }

  runtime_module_ = LoadLibraryExW(
      runtime_path.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!runtime_module_) {
    // Full-path fallback for older loader configurations. The official
    // librime binary only imports Windows system DLLs.
    runtime_module_ = LoadLibraryW(runtime_path.c_str());
  }
  if (!runtime_module_) {
    LogFailure(L"engine-failed-loadlibrary", GetLastError());
    return false;
  }

  using RimeGetApi = RimeApi*(__cdecl*)();
  const auto get_api = reinterpret_cast<RimeGetApi>(
      GetProcAddress(runtime_module_, "rime_get_api"));
  if (!get_api) {
    LogEvent(L"engine-failed", L"rime_get_api missing from rime.dll");
    FreeLibrary(runtime_module_);
    runtime_module_ = nullptr;
    return false;
  }
  runtime_api_ = get_api();
  api_ = runtime_api_;
  if (!api_) {
    LogEvent(L"engine-failed", L"rime_get_api returned null");
    FreeLibrary(runtime_module_);
    runtime_module_ = nullptr;
    return false;
  }

  std::filesystem::path shared = module_dir.parent_path() / L"data";
  if (!std::filesystem::exists(shared / L"zuxia.schema.yaml")) {
    LogEvent(L"engine-failed", L"schema not found under " + shared.wstring());
    return false;
  }
  LoadCodeHints(shared.wstring());
  std::filesystem::path user(LocalAppDataDirectory());
  user /= L"Zuxia";
  user /= L"Rime";

  std::error_code error;
  std::filesystem::create_directories(user, error);
  if (error) {
    // Typically an AppContainer host with no write access to the user
    // profile; librime will fail to deploy and the service will stay idle.
    LogFailure(L"user-dir-not-writable",
               static_cast<unsigned long>(error.value()));
  }

  try {
    shared_data_utf8_ = WideToUtf8(shared.wstring());
    user_data_utf8_ = WideToUtf8(user.wstring());
  } catch (...) {
    LogEvent(L"engine-failed", L"cannot encode data paths as UTF-8");
    return false;
  }

  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = shared_data_utf8_.c_str();
  traits.user_data_dir = user_data_utf8_.c_str();
  traits.distribution_name = "Zuxia IME";
  traits.distribution_code_name = "zuxia";
  traits.distribution_version = "0.2.0";
  traits.app_name = "rime.zuxia";
  traits.min_log_level = 2;
  traits.log_dir = "";

  api_->setup(&traits);
  api_->initialize(&traits);
  const DWORD started = GetTickCount();
  if (api_->start_maintenance(False)) {
    // First run on this account compiles the dictionaries; it takes seconds,
    // and the timing is the first thing to check when startup feels stuck.
    api_->join_maintenance_thread();
    wchar_t elapsed[32] = {};
    swprintf_s(elapsed, L"%lu ms", GetTickCount() - started);
    LogEvent(L"dictionaries-built", elapsed);
  }
  wchar_t hints[48] = {};
  swprintf_s(hints, L"%zu characters", code_hints_.size());
  LogEvent(L"engine-ready", hints);
  return true;
}

std::wstring RimeEngine::ModuleDirectory(HMODULE module) {
  std::wstring path(32768, L'\0');
  const DWORD length =
      GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  if (!length || length >= path.size()) return L".";
  path.resize(length);
  return std::filesystem::path(path).parent_path().wstring();
}

std::wstring RimeEngine::LocalAppDataDirectory() {
  wchar_t path[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                 nullptr, SHGFP_TYPE_CURRENT, path))) {
    return path;
  }
  return L".";
}

// Asks the active keyboard layout what this key produces right now, honouring
// Shift and AltGr. ToUnicodeEx is given the "do not change keyboard state"
// flag so that probing a key never swallows a pending dead key.
int RimeEngine::AsciiForKey(WPARAM virtual_key) {
  BYTE state[256] = {};
  if (!GetKeyboardState(state)) return 0;
  // Ctrl/Alt combinations belong to the application, never to the engine.
  if ((state[VK_CONTROL] & 0x80) || (state[VK_MENU] & 0x80)) return 0;

  const HKL layout = GetKeyboardLayout(0);
  const UINT scan = MapVirtualKeyExW(static_cast<UINT>(virtual_key),
                                     MAPVK_VK_TO_VSC, layout);
  wchar_t buffer[8] = {};
  const int written = ToUnicodeEx(static_cast<UINT>(virtual_key), scan, state,
                                  buffer, ARRAYSIZE(buffer), 1 << 2, layout);
  if (written != 1) return 0;
  const wchar_t ch = buffer[0];
  return (ch >= 0x20 && ch < 0x7F) ? static_cast<int>(ch) : 0;
}

bool RimeEngine::IsPunctuationKey(WPARAM virtual_key) {
  const int ch = AsciiForKey(virtual_key);
  if (!ch || ch == ' ') return false;
  // Letters and digits already have their own handling; this is only about
  // the marks that should come out full-width while typing Chinese.
  return !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9'));
}

int RimeEngine::VirtualKeyToRimeKey(WPARAM virtual_key) {
  if (virtual_key >= 'A' && virtual_key <= 'Z') {
    return static_cast<int>(virtual_key - 'A' + 'a');
  }
  if (virtual_key >= '0' && virtual_key <= '9') {
    return static_cast<int>(virtual_key);
  }
  switch (virtual_key) {
    case VK_SPACE:
      return ' ';
    case VK_BACK:
      return kXkBackSpace;
    case VK_RETURN:
      return kXkReturn;
    case VK_ESCAPE:
      return kXkEscape;
    case VK_HOME:
      return kXkHome;
    case VK_LEFT:
      return kXkLeft;
    case VK_UP:
      return kXkUp;
    case VK_RIGHT:
      return kXkRight;
    case VK_DOWN:
      return kXkDown;
    case VK_PRIOR:
      return kXkPageUp;
    case VK_NEXT:
      return kXkPageDown;
    case VK_END:
      return kXkEnd;
    case VK_DELETE:
      return kXkDelete;
    default:
      // Punctuation and the other printable marks; librime's punctuator turns
      // them into their Chinese forms.
      return AsciiForKey(virtual_key);
  }
}

}  // namespace zuxia