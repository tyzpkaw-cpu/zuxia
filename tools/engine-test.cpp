// Exercises the shipping RimeEngine against the 足下 scheme: candidate
// annotation, reading selection for polyphonic characters, Chinese
// punctuation and the Western-mode toggle.
//
// Must run from <root>\x64\ so RimeEngine finds rime.dll beside it and the
// data directory at ..\data, exactly as the text service does:
//
//     build\engine-test-x64.bat        (or the command in tools\README.md)
//     cd dist\Zuxia\x64
//     ..\..\..\tools\engine-test.exe
//
// 足下 has no phrase dictionary yet, so every case here is a single character.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "RimeEngine.h"

namespace {

void Print(const std::wstring& text) {
  if (text.empty()) return;
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return;
  std::string buffer(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, buffer.data(), size,
                      nullptr, nullptr);
  fputs(buffer.c_str(), stdout);
}

int failures = 0;

void Case(zuxia::RimeEngine* engine, const char* keys, int show) {
  engine->Clear();
  zuxia::EngineSnapshot snapshot;
  for (const char* p = keys; *p; ++p) {
    snapshot = engine->ProcessKey(static_cast<int>(*p), 0);
  }
  printf("  %-12s -> ", keys);
  const int total = static_cast<int>(snapshot.candidates.size());
  const int n = total < show ? total : show;
  for (int i = 0; i < n; ++i) {
    const zuxia::Candidate& candidate = snapshot.candidates[i];
    printf(" ");
    Print(candidate.text);
    if (!candidate.comment.empty()) {
      printf("[");
      Print(candidate.comment);
      printf("]");
    }
  }
  if (total == 0) printf(" (no candidates)");
  printf("\n");
}

void Expect(const char* label, bool ok) {
  printf("  %-52s %s\n", label, ok ? "PASS" : "FAIL");
  if (!ok) ++failures;
}

std::wstring CommentOf(zuxia::RimeEngine* engine, const char* keys,
                       const wchar_t* want_text) {
  engine->Clear();
  zuxia::EngineSnapshot snapshot;
  for (const char* p = keys; *p; ++p) {
    snapshot = engine->ProcessKey(static_cast<int>(*p), 0);
  }
  for (const zuxia::Candidate& candidate : snapshot.candidates) {
    if (candidate.text == want_text) return candidate.comment;
  }
  return L"(not found)";
}

// 注释里的码随部件别名表变化 —— 给 青 加一条「里面看得见月」，清 就从两个
// 满码变成四个。所以断言不能钉死字面量，否则每加一条别名就要改一次测试，
// 而它其实什么都没测出来。这里断言的是不随别名改变的两条性质：
//
//   1. 注释里的每个码都以已经打出的那几键开头 —— 承诺的码必须真的打得到。
//      AnnotateCode 在这里出过 bug：`行` 在 hang 下会多出一个 `…`，指向一个
//      根本不存在的码。
//   2. 某个明确该在的码确实在。
std::vector<std::wstring> SplitCodes(const std::wstring& comment) {
  std::vector<std::wstring> out;
  std::wstring current;
  for (wchar_t ch : comment) {
    if (ch == L' ') {
      if (!current.empty()) out.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

bool EveryCodeStartsWith(const std::wstring& comment, const wchar_t* prefix) {
  const std::wstring want(prefix);
  bool saw_one = false;
  for (const std::wstring& code : SplitCodes(comment)) {
    if (code == L"\u2026") continue;  // 截断省略号不是码
    saw_one = true;
    if (code.size() < want.size()) return false;
    if (code.compare(0, want.size(), want) != 0) return false;
  }
  return saw_one;
}

bool Mentions(const std::wstring& comment, const wchar_t* code) {
  for (const std::wstring& one : SplitCodes(comment)) {
    if (one == code) return true;
  }
  return false;
}

bool Offers(zuxia::RimeEngine* engine, const char* keys,
            const wchar_t* want_text) {
  return CommentOf(engine, keys, want_text) != L"(not found)";
}

// 词表里没有的组合靠 enable_sentence 逐字拼。断言「出了个两字候选」而不是
// 某个具体的词：拼出来是 苏瑶 还是 素要 由字频决定，会随词表变。
bool OffersLength(zuxia::RimeEngine* engine, const char* keys, size_t len) {
  engine->Clear();
  zuxia::EngineSnapshot snapshot;
  for (const char* p = keys; *p; ++p) {
    snapshot = engine->ProcessKey(static_cast<int>(*p), 0);
  }
  for (const zuxia::Candidate& candidate : snapshot.candidates) {
    if (candidate.text.size() == len) return true;
  }
  return false;
}

}  // namespace

int main() {
  SetConsoleOutputCP(CP_UTF8);
  zuxia::RimeEngine engine;
  printf("initializing (first run compiles dictionaries)...\n");
  fflush(stdout);
  if (!engine.Initialize(nullptr)) {
    printf("FAILED: engine did not initialize\n");
    return 1;
  }
  printf("ready\n\n");

  printf("candidates with their complete code:\n");
  Case(&engine, "qing", 5);
  Case(&engine, "qingz", 5);
  Case(&engine, "qingzs", 5);
  Case(&engine, "qingzq", 5);
  Case(&engine, "qingzsq", 5);
  Case(&engine, "zi", 5);
  Case(&engine, "zisbz", 5);
  Case(&engine, "hang", 5);
  Case(&engine, "xing", 5);
  Case(&engine, "tiandi", 5);
  Case(&engine, "tiandidz", 5);
  Case(&engine, "tiandidzyt", 5);

  printf("\nchecks:\n");
  // 清 = 氵(水 s) + 青(q, 另有别名 月 y), 左右 structure (z)。满码是
  // qing + z + 任意两个部件字母，顺序不限。
  Expect("清 under `qingzs` only promises codes starting with qingzs",
         EveryCodeStartsWith(CommentOf(&engine, "qingzs", L"清"), L"qingzs"));
  Expect("清 under `qingzs` still offers qingzsq",
         Mentions(CommentOf(&engine, "qingzs", L"清"), L"qingzsq"));
  Expect("清 under `qingzq` only promises codes starting with qingzq",
         EveryCodeStartsWith(CommentOf(&engine, "qingzq", L"清"), L"qingzq"));
  Expect("清 under `qingzq` still offers qingzqs",
         Mentions(CommentOf(&engine, "qingzq", L"清"), L"qingzqs"));
  Expect("清 under bare `qing` only promises codes starting with qing",
         EveryCodeStartsWith(CommentOf(&engine, "qing", L"清"), L"qing"));
  // The structure key alone already narrows things down.
  Expect("清 survives the structure key `qingz`",
         EveryCodeStartsWith(CommentOf(&engine, "qingz", L"清"), L"qingz"));
  // 字 = 宀(宝盖 b) + 子(z), 上下 structure (s).
  Expect("字 under `zisb` is annotated zisbz alone",
         CommentOf(&engine, "zisb", L"字") == L"zisbz");
  // 行 has three readings; only the one being typed should show, and each
  // reading carries three complete codes (any two of 彳/一/亍 in either
  // order) -- which is exactly why the annotation is capped.
  Expect("行 under `hang` shows only its hang codes",
         EveryCodeStartsWith(CommentOf(&engine, "hang", L"行"), L"hang"));
  Expect("行 under `xing` shows only its xing codes",
         EveryCodeStartsWith(CommentOf(&engine, "xing", L"行"), L"xing"));

  // 词组：全拼 -> 加逐位结构串 -> 加逐位部件串。天 独体(d) 一(y),
  // 地 左右(z) 土(t)。三档都要能打出来 —— 0.2.0 装出来时一档都打不出，
  // 因为词组码表根本没生成。
  printf("\nphrases:\n");
  Expect("天地 under bare pinyin `tiandi`", Offers(&engine, "tiandi", L"天地"));
  Expect("天地 under `tiandidz` (structure column)",
         Offers(&engine, "tiandidz", L"天地"));
  Expect("天地 under `tiandidzyt` (component column)",
         Offers(&engine, "tiandidzyt", L"天地"));
  Expect("你好 under bare pinyin `nihao`", Offers(&engine, "nihao", L"你好"));
  Expect("你好 under `nihaozzrn` (full columnar code)",
         Offers(&engine, "nihaozzrn", L"你好"));
  // 苏瑶 是个人名，rime-ice 的 12 万词里没有。全拼那一档要有东西出来。
  Expect("词表外的 `suyao` 仍拼得出一个两字候选",
         OffersLength(&engine, "suyao", 2));

  // Chinese text wants Chinese marks. The engine is handed the plain ASCII
  // character; librime's punctuator is what turns it into the full-width form.
  printf("\nChinese punctuation:\n");
  struct Mark {
    char key;
    const wchar_t* want;
    const char* name;
  };
  const Mark marks[] = {
      {',', L"，", "comma"},     {'.', L"。", "full stop"},
      {'?', L"？", "question"},  {'!', L"！", "exclamation"},
      {';', L"；", "semicolon"}, {':', L"：", "colon"},
      {'\\', L"、", "enumeration comma"},
  };
  for (const Mark& mark : marks) {
    engine.Clear();
    const zuxia::EngineSnapshot snapshot =
        engine.ProcessKey(static_cast<int>(mark.key), 0);
    char label[72] = {};
    sprintf_s(label, "%s becomes its Chinese form", mark.name);
    Expect(label, snapshot.commit == mark.want);
  }

  printf("\nWestern-mode toggle:\n");
  engine.Clear();
  Expect("starts in Chinese mode", !engine.IsAsciiMode());
  Expect("toggle turns Western mode on", engine.ToggleAsciiMode());
  Expect("IsAsciiMode agrees", engine.IsAsciiMode());
  Expect("toggle turns it back off", !engine.ToggleAsciiMode());
  Expect("back in Chinese mode", !engine.IsAsciiMode());
  engine.Clear();
  Expect("Chinese mode still produces candidates",
         !CommentOf(&engine, "qingzs", L"清").empty());

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
         failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
