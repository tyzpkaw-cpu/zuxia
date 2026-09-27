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
  Case(&engine, "qings", 5);
  Case(&engine, "qingq", 5);
  Case(&engine, "qingsq", 5);
  Case(&engine, "zi", 5);
  Case(&engine, "zibz", 5);
  Case(&engine, "hang", 5);
  Case(&engine, "xing", 5);
  Case(&engine, "tiandi", 5);

  printf("\nchecks:\n");
  // 清 = 氵(水 s) + 青(q). Both component orders are legal, so it has two
  // complete codes; the annotation shows whichever ones the typed prefix
  // still admits.
  Expect("清 under `qings` is annotated qingsq alone",
         CommentOf(&engine, "qings", L"清") == L"qingsq");
  Expect("清 under `qingq` is annotated qingqs alone",
         CommentOf(&engine, "qingq", L"清") == L"qingqs");
  Expect("清 under bare `qing` shows both complete codes",
         CommentOf(&engine, "qing", L"清") == L"qingqs qingsq");
  // 字 = 宀(宝盖 b) + 子(z).
  Expect("字 under `zib` is annotated zibz alone",
         CommentOf(&engine, "zib", L"字") == L"zibz");
  // 行 has three readings; only the one being typed should show, and each
  // reading carries three complete codes (any two of 彳/一/亍 in either
  // order) -- which is exactly why the annotation is capped.
  Expect("行 under `hang` shows only its hang codes",
         CommentOf(&engine, "hang", L"行") == L"hangcc hangcs hangsc");
  Expect("行 under `xing` shows only its xing codes",
         CommentOf(&engine, "xing", L"行") == L"xingcc xingcs xingsc");

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
         !CommentOf(&engine, "qings", L"清").empty());

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
         failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
