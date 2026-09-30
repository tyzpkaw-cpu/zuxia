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

std::mutex RimeEngine::runtime_mutex_;
bool RimeEngine::runtime_tried_ = false;
unsigned long RimeEngine::runtime_last_try_ = 0;
bool RimeEngine::runtime_ready_ = false;
HMODULE RimeEngine::runtime_module_ = nullptr;
RimeApi* RimeEngine::runtime_api_ = nullptr;
std::string RimeEngine::shared_data_utf8_;
std::wstring RimeEngine::shared_data_dir_;
ColumnarDecoder RimeEngine::decoder_;
bool RimeEngine::decoder_ready_ = false;
std::mutex RimeEngine::decoder_mutex_;
bool RimeEngine::decoder_tried_ = false;
unsigned long RimeEngine::decoder_last_try_ = 0;
std::string RimeEngine::user_data_utf8_;
std::unordered_map<std::wstring, std::vector<std::wstring>>
    RimeEngine::code_hints_;

RimeEngine::RimeEngine() = default;

RimeEngine::~RimeEngine() { Shutdown(); }

bool RimeEngine::Initialize(HMODULE module) {
  if (Ready()) return true;
  // EnsureDecoder 那个坑的双胞胎，而且更狠：这里管的是整个 rime.dll 加载和
  // 数据目录解析，原来用 std::call_once 包着，而 runtime_ready_ 是进程级静态
  // 量 —— 一个宿主进程首次初始化失败，它这辈子就再也没有输入法，连重试都不
  // 会。而首次失败的常见原因恰恰是暂时的：升级时 rime.dll 正被替换。失败要
  // 能重试，带冷却，免得一个坏安装每次按键都重来一遍。
  if (!runtime_ready_) {
    constexpr unsigned long kRetryMs = 30 * 1000;
    std::lock_guard<std::mutex> guard(runtime_mutex_);
    if (!runtime_ready_) {
      const unsigned long now = GetTickCount();
      if (runtime_tried_ && now - runtime_last_try_ < kRetryMs) return false;
      runtime_tried_ = true;
      runtime_last_try_ = now;
      runtime_ready_ = InitializeRuntime(module);
    }
  }
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

// 这些成员是进程级静态量，原来用 std::call_once 包着：一个宿主进程首次加载
// 失败，它这辈子就再也不试了。而「首次加载失败」最常见的原因恰恰是暂时的 ——
// 应用正开着的时候升级输入法，数据文件有一瞬间不在。所以失败要能重试，但不能
// 每次按键都去读一遍 3 MB，于是加了冷却时间。
bool RimeEngine::EnsureDecoder() {
  if (decoder_ready_) return true;
  if (shared_data_dir_.empty()) return false;

  constexpr unsigned long kRetryMs = 30 * 1000;
  std::lock_guard<std::mutex> guard(decoder_mutex_);
  if (decoder_ready_) return true;
  const unsigned long now = GetTickCount();
  // 无符号回绕相减照样给出正确的间隔，GetTickCount 每 49 天归零不影响这里。
  if (decoder_tried_ && now - decoder_last_try_ < kRetryMs) return false;
  decoder_tried_ = true;
  decoder_last_try_ = now;

  std::filesystem::path path(shared_data_dir_);
  path /= L"zuxia.decoder.tsv";
  unsigned long error = 0;
  decoder_ready_ = decoder_.Load(path.wstring(), &error);
  if (decoder_ready_) {
    // 回流表。与只读的主表分开，放在用户自己的目录里，升级不会覆盖它。
    std::filesystem::path learned(LocalAppDataDirectory());
    learned /= L"Zuxia";
    learned /= L"zuxia.decoder.user.tsv";
    decoder_.SetUserTable(learned.wstring());
    LogEvent(L"decoder-loaded", path.wstring());
  } else {
    wchar_t code[24] = {};
    swprintf_s(code, L" (0x%08lX)", error);
    LogEvent(L"decoder-unavailable", path.wstring() + code);
  }
  return decoder_ready_;
}

// 列式码（全拼串＋逐位结构串＋逐位部件串）Rime 的分词器切不动：结构位和
// 部件位与它们描述的那个字并不相邻，所以解码器必须自己出候选。
//
// 它原先只在 Rime 一个候选都给不出来时才上场。那个门槛定错了 —— Rime 给出
// 候选不等于给对。打 woxiangwen 它只能靠补全凑出「我想问问 ~wen」，打
// xuancibz 它读成「选＋疵」；两次都有候选，两次都不是要的词，而解码器把
// 「我想问」排在第一位。有候选就闭嘴，等于把解码器锁死在最需要它的场合外。
//
// 现在改成补位：Rime 的候选原样排在前面，解码器填这一页剩下的空位。打得准
// 的时候第一位一个字不动，打到词库覆盖不到的地方，第二第三位就有救。
void RimeEngine::FillDecodedCandidates(EngineSnapshot* out) {
  // 候选窗一页放几条。必须与 data/zuxia.schema.yaml 的 menu/page_size 相同，
  // 否则补位要么填不满要么溢出一页；data-tools/audit_zuxia.py 逐版核对。
  constexpr size_t kPageSize = 9;
  overlay_.clear();
  overlay_code_.clear();
  overlay_tail_.clear();
  overlay_base_ = 0;
  if (!out || out->preedit.empty()) return;
  // 翻过页之后这一页整页都是 Rime 的，补位只发生在第一页。
  if (out->page_no != 0) return;
  const size_t taken = out->candidates.size();
  if (taken >= kPageSize) return;
  const char* raw = api_->get_input(session_);
  if (!raw || !*raw) return;
  const std::string keys(raw);
  if (keys.size() < 3) return;
  if (!EnsureDecoder()) return;

  const size_t room = kPageSize - taken;
  std::vector<std::wstring> words;
  std::string tail;
  try {
    // 多要一些：与 Rime 重复的要丢掉，丢完还得填得满。
    words = decoder_.Decode(keys, room + taken, &tail);
  } catch (...) {
    return;
  }
  // 兜底候选只用掉了码的前一段。剩下的那几位必须能原样交回 Rime，所以先
  // 确认它确实是这串码的后缀；确认不了就整批不要 —— 端出一个会吞掉用户
  // 按键的候选，比不端出来糟得多。
  std::string used = keys;
  if (!tail.empty()) {
    if (tail.size() >= keys.size() ||
        keys.compare(keys.size() - tail.size(), tail.size(), tail) != 0) {
      return;
    }
    used = keys.substr(0, keys.size() - tail.size());
  }
  for (const std::wstring& word : words) {
    if (overlay_.size() >= room) break;
    bool duplicate = false;
    for (const Candidate& shown : out->candidates) {
      if (shown.text == word) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    Candidate one;
    one.text = word;
    one.label = std::to_wstring(taken + overlay_.size() + 1);
    out->candidates.push_back(one);
    overlay_.push_back(word);
  }
  if (!overlay_.empty()) {
    overlay_base_ = taken;
    // Rime 一个都没给时解码器的第一条就是首选；否则高亮归 Rime。
    if (taken == 0) out->highlighted = 0;
    // 选中之后要把这串码和选中的字一起记进回流表，所以码得留到那时候。
    // 兜底时记的是真正用上的那段前缀，否则下次打同样的码前置不到。
    overlay_code_ = used;
    overlay_tail_ = tail;
  }
}

EngineSnapshot RimeEngine::ProcessKey(int keycode, int modifiers) {
  if (!Ready()) return {};
  // 解码器交出去的候选 Rime 不知道，选择键得在这里截下来自己落字。落在
  // overlay_base_ 之前的下标是 Rime 的候选，必须原样放过去。
  if (!overlay_.empty() && modifiers == 0) {
    int pick = -1;
    if (keycode >= '1' && keycode <= '9') pick = keycode - '1';
    // 空格落的是高亮那条。Rime 有候选时高亮是它的，空格就该归它。
    else if (keycode == ' ' && overlay_base_ == 0) pick = 0;
    const size_t base = overlay_base_;
    if (pick >= 0 && static_cast<size_t>(pick) >= base &&
        static_cast<size_t>(pick) - base < overlay_.size()) {
      EngineSnapshot out;
      out.handled = true;
      out.commit = overlay_[static_cast<size_t>(pick) - base];
      // 回流：这是用户在这串码上的选择，记下来，下次同一串码直接前置。
      decoder_.RecordChoice(overlay_code_, out.commit);
      const std::string tail = overlay_tail_;
      overlay_.clear();
      overlay_code_.clear();
      overlay_tail_.clear();
      overlay_base_ = 0;
      api_->clear_composition(session_);
      // 兜底候选只吃掉了码的前一段，剩下那几位是用户真按过的键，得原样
      // 送回去接着组字。落字与组字要在同一个快照里交出去，所以
      // CTextService::_ApplyRimeSnapshot 必须两样都处理 —— 它以前只处理
      // 落字，preedit 会被丢掉。
      if (!tail.empty()) {
        for (char key : tail) {
          api_->process_key(
              session_, static_cast<int>(static_cast<unsigned char>(key)), 0);
        }
        EngineSnapshot after = ReadSnapshot(true);
        // 理论上喂几个字母不会触发落字，真触发了也不能丢。
        out.commit += after.commit;
        out.composing = after.composing;
        out.preedit = std::move(after.preedit);
        out.candidates = std::move(after.candidates);
        out.highlighted = after.highlighted;
        out.page_no = after.page_no;
        out.last_page = after.last_page;
        FillDecodedCandidates(&out);
      }
      return out;
    }
  }
  const bool handled = api_->process_key(session_, keycode, modifiers);
  EngineSnapshot out = ReadSnapshot(handled);
  FillDecodedCandidates(&out);
  return out;
}

EngineSnapshot RimeEngine::Snapshot() {
  if (!Ready()) return {};
  return ReadSnapshot(false);
}

void RimeEngine::Clear() {
  overlay_.clear();
  overlay_code_.clear();
  overlay_tail_.clear();
  overlay_base_ = 0;
  if (Ready()) api_->clear_composition(session_);
}

std::string RimeEngine::RawInput() const {
  if (!initialized_ || !api_ || !session_) return std::string();
  const char* raw = api_->get_input(session_);
  return raw ? std::string(raw) : std::string();
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
  // An application that is already running when the IME is upgraded keeps
  // the old DLL mapped until it restarts, and then behaves like a version
  // that is no longer installed. The build stamp is the only thing in the
  // log that distinguishes which copy a given host actually loaded.
  const std::string stamp(__DATE__ " " __TIME__);
  LogEvent(L"engine-start", module_dir.wstring() + L" built " +
                                std::wstring(stamp.begin(), stamp.end()));
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
  shared_data_dir_ = shared.wstring();
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