// Linux self-test for src/Decoder.cpp.
//
//   g++ -std=c++17 -O2 -Itools/linux-selftest -Isrc \
//       -o /tmp/dselftest tools/linux-selftest/decoder-selftest.cpp src/Decoder.cpp
//   cd <repo root> && /tmp/dselftest
//
// Compiles Decoder.cpp against a small Win32 stub (tools/linux-selftest/
// windows.h) so the columnar decoder can be exercised without Windows.
#include "Decoder.h"

#include <windows.h>

#include <cerrno>

#include <cstdio>
#include <string>
#include <vector>

// --- Win32 shims (declared in ./windows.h) ---------------------------------
static std::string Narrow(const wchar_t* wide) {
  std::string out;
  for (const wchar_t* p = wide; *p; ++p) {
    const unsigned int cp = static_cast<unsigned int>(*p);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

HANDLE CreateFileW(LPCWSTR name, DWORD access, DWORD, LPSECURITY_ATTRIBUTES,
                   DWORD disposition, DWORD, HANDLE) {
  // 回流表要能追加也要能整体重写，所以这个桩得照 access/disposition 选模式。
  const char* mode = "rb";
  if (access & FILE_APPEND_DATA) {
    mode = "ab";
  } else if (access & GENERIC_WRITE) {
    mode = (disposition == CREATE_ALWAYS) ? "wb" : "r+b";
  }
  FILE* file = std::fopen(Narrow(name).c_str(), mode);
  return file ? reinterpret_cast<HANDLE>(file) : INVALID_HANDLE_VALUE;
}

BOOL WriteFile(HANDLE handle, LPCVOID buffer, DWORD to_write, DWORD* written,
               LPOVERLAPPED) {
  const size_t put = std::fwrite(buffer, 1, to_write,
                                 reinterpret_cast<FILE*>(handle));
  if (written) *written = static_cast<DWORD>(put);
  return put == static_cast<size_t>(to_write);
}

BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER* size) {
  FILE* file = reinterpret_cast<FILE*>(handle);
  const long current = std::ftell(file);
  if (current < 0 || std::fseek(file, 0, SEEK_END) != 0) return 0;
  const long length = std::ftell(file);
  std::fseek(file, current, SEEK_SET);
  if (length < 0) return 0;
  size->QuadPart = length;
  return 1;
}

BOOL ReadFile(HANDLE handle, LPVOID buffer, DWORD to_read, DWORD* read, LPOVERLAPPED) {
  const size_t got = std::fread(buffer, 1, to_read, reinterpret_cast<FILE*>(handle));
  *read = static_cast<DWORD>(got);
  return got == static_cast<size_t>(to_read);
}

BOOL CloseHandle(HANDLE handle) {
  return std::fclose(reinterpret_cast<FILE*>(handle)) == 0;
}

// The decoder reports why a load failed. errno is the nearest equivalent the
// C library offers, and the value only has to be printable here.
DWORD GetLastError(void) { return static_cast<DWORD>(errno); }

// --- test cases ------------------------------------------------------------
static int failures = 0;
static int checks = 0;

static std::string Utf8(const std::wstring& wide) {
  std::string out;
  for (wchar_t ch : wide) {
    const unsigned int cp = static_cast<unsigned int>(ch);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

static void Check(const char* keys, const char* expected_first, int min_count,
                  zuxia::ColumnarDecoder* decoder) {
  ++checks;
  const std::vector<std::wstring> words = decoder->Decode(keys, 9);
  std::string got;
  for (size_t i = 0; i < words.size(); ++i) {
    if (i) got += "/";
    got += Utf8(words[i]);
  }
  std::printf("%-22s -> %s\n", keys, got.empty() ? "(empty)" : got.c_str());
  const bool ok =
      min_count == 0
          ? words.empty()
          : (!words.empty() && Utf8(words[0]) == expected_first &&
             static_cast<int>(words.size()) >= min_count);
  if (!ok) {
    ++failures;
    std::printf("  FAIL: want %s (at least %d)\n", expected_first, min_count);
  }
}


// 死码兜底：整串拼不出来时退到最长有效前缀，并如实报出没用上的尾巴。
// 尾巴那一项是硬要求 —— 输入法那边靠它把用户按过的键喂回去。
static void CheckFallback(const char* keys, const char* expected_first,
                          const char* expected_tail,
                          zuxia::ColumnarDecoder* decoder) {
  ++checks;
  std::string tail;
  const std::vector<std::wstring> words = decoder->Decode(keys, 9, &tail);
  std::string got;
  for (size_t i = 0; i < words.size() && i < 3; ++i) {
    if (i) got += "/";
    got += Utf8(words[i]);
  }
  std::printf("%-22s -> %-24s tail=%s\n", keys,
              got.empty() ? "(empty)" : got.c_str(),
              tail.empty() ? "(none)" : tail.c_str());
  const bool want_empty = expected_first[0] == 0;
  const bool ok = want_empty
                      ? (words.empty() && tail.empty())
                      : (!words.empty() && Utf8(words[0]) == expected_first &&
                         tail == expected_tail);
  if (!ok) {
    ++failures;
    std::printf("  FAIL: want %s tail=%s\n",
                want_empty ? "(empty)" : expected_first, expected_tail);
  }
}

// 结构位只认 zsbpd。别的字母不许被当成结构位解出词来 —— 那说明分段规则
// 漏了。注意兜底是另一回事：退到前缀之后出结果是允许的，尾巴非空就证明
// 它没把那几位当结构位用。所以这里判的是「整串码本身解出了什么」。
static void CheckRejected(const char* keys, zuxia::ColumnarDecoder* decoder) {
  ++checks;
  std::string tail;
  const std::vector<std::wstring> words = decoder->Decode(keys, 9, &tail);
  const bool ok = words.empty() || !tail.empty();
  std::printf("%-22s -> %s tail=%s\n", keys,
              words.empty() ? "(empty)" : Utf8(words[0]).c_str(),
              tail.empty() ? "(none)" : tail.c_str());
  if (!ok) {
    ++failures;
    std::printf("  FAIL: the whole code must not decode\n");
  }
}

static void Assert(const char* label, bool ok) {
  ++checks;
  std::printf("  %-52s %s\n", label, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

static std::wstring Wide(const char* utf8) {
  std::wstring out;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8);
  while (*p) {
    unsigned int cp = *p++;
    if (cp >= 0xF0) { cp = ((cp & 0x07) << 18); cp |= (*p++ & 0x3F) << 12;
                      cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
    else if (cp >= 0xE0) { cp = ((cp & 0x0F) << 12); cp |= (*p++ & 0x3F) << 6;
                           cp |= (*p++ & 0x3F); }
    else if (cp >= 0xC0) { cp = ((cp & 0x1F) << 6); cp |= (*p++ & 0x3F); }
    out.push_back(static_cast<wchar_t>(cp));
  }
  return out;
}

static long long SizeOf(const char* path) {
  FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, 0, SEEK_END);
  const long long n = std::ftell(f);
  std::fclose(f);
  return n;
}

// 回流：选过的结果下次要排在最前，而且顺序只看「最近用过」。
static void CheckRecall(zuxia::ColumnarDecoder* decoder) {
  const char* path = "/tmp/zuxia-decoder-user-selftest.tsv";
  std::remove(path);
  wchar_t wide[512] = {};
  for (size_t i = 0; path[i] && i < 511; ++i)
    wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(path[i]));

  const char* kCode = "yangzhipengzszmsp";
  const char* kPeng = "\xe6\x9d\xa8\xe5\xbf\x97\xe9\xb9\x8f";  // 杨志鹏
  const char* kPeng2 = "\xe6\x9d\xa8\xe5\xbf\x97\xe6\xa3\x9a"; // 杨志棚
  const char* kPeng3 = "\xe6\x9d\xa8\xe5\xbf\x97\xe8\x86\xa8"; // 杨志膨

  std::printf("\nrecall (user table):\n");
  decoder->SetUserTable(wide);

  // 没有表的时候不能改变任何东西。
  std::vector<std::wstring> words = decoder->Decode(kCode, 9);
  Assert("no table yet: beam search order unchanged",
         !words.empty() && Utf8(words[0]) == kPeng);

  // 选一次就该生效 —— 不是攒够几次才入库。
  decoder->RecordChoice(kCode, Wide(kPeng2));
  const long long after_first = SizeOf(path);
  words = decoder->Decode(kCode, 9);
  Assert("one pick is enough: it is first next time",
         !words.empty() && Utf8(words[0]) == kPeng2);

  // 追加必须真的追加。日志就是在这一点上把自己覆写掉的。
  decoder->RecordChoice(kCode, Wide(kPeng3));
  const long long after_second = SizeOf(path);
  Assert("the table grows on the first write", after_first > 0);
  Assert("a second write appends instead of overwriting",
         after_second > after_first);

  // 最近用过的在最前，上一次的退到第二 —— 不按次数排，所以一步就能翻回来。
  words = decoder->Decode(kCode, 9);
  Assert("most recent pick comes first",
         !words.empty() && Utf8(words[0]) == kPeng3);
  Assert("the previous pick is right behind it",
         words.size() > 1 && Utf8(words[1]) == kPeng2);

  // 前置名额有限，beam search 的结果还在,而且没有重复。
  bool duplicated = false;
  for (size_t i = 0; i < words.size(); ++i)
    for (size_t j = i + 1; j < words.size(); ++j)
      if (words[i] == words[j]) duplicated = true;
  Assert("no duplicates between learned and searched", !duplicated);
  Assert("beam search results are still there", words.size() >= 5);

  // 别的码一点不受影响。
  const std::vector<std::wstring> other = decoder->Decode("suyaoszcw", 9);
  Assert("an unrelated code is untouched",
         !other.empty() && Utf8(other[0]) == "\xe8\x8b\x8f\xe7\x91\xb6");

  // 坏行跳过，不能连坐。
  FILE* f = std::fopen(path, "ab");
  if (f) {
    std::fputs("no-tab-here\n\t\nzz\t\n", f);
    std::fclose(f);
  }
  words = decoder->Decode(kCode, 9);
  Assert("malformed lines are skipped, learned order survives",
         !words.empty() && Utf8(words[0]) == kPeng3);

  // 另一个进程追加过,长度变了就该重读 —— 这是跨程序即时生效那条性质。
  f = std::fopen(path, "ab");
  if (f) {
    std::fprintf(f, "%s\t%s\n", kCode, kPeng);
    std::fclose(f);
  }
  words = decoder->Decode(kCode, 9);
  Assert("a pick made by another process is picked up",
         !words.empty() && Utf8(words[0]) == kPeng);

  decoder->SetUserTable(std::wstring());
  std::remove(path);
  words = decoder->Decode(kCode, 9);
  Assert("with the table detached, nothing is prepended",
         !words.empty() && Utf8(words[0]) == kPeng);

  // 回流要跟着兜底一起退。兜底时记下的是那段前缀，所以查也必须按前缀
  // 查 —— 不然用户教过一次的词，在同一串打错的码上第二次还是不排前面。
  std::remove(path);
  decoder->SetUserTable(wide);
  const char* kZuxia = "zuxiasdk";
  const char* kDead = "zuxiasdkh";   // 多打了一个 h，靠兜底退回 zuxiasdk
  const char* kZu = "\xe8\xb6\xb3\xe4\xb8\x8b";   // 足下
  std::string tail;
  words = decoder->Decode(kDead, 9, &tail);
  Assert("the dead code falls back and reports its tail",
         !words.empty() && Utf8(words[0]) == kZu && tail == "h");
  // 从兜底候选里选的是「前缀 -> 词」这一条，RimeEngine 就是这么记的。
  decoder->RecordChoice(kZuxia, Wide(kZu));
  words = decoder->Decode(kDead, 9, &tail);
  Assert("a pick learned on the prefix still leads on the dead code",
         !words.empty() && Utf8(words[0]) == kZu && tail == "h");
  bool dup = false;
  for (size_t i = 0; i < words.size(); ++i)
    for (size_t j = i + 1; j < words.size(); ++j)
      if (words[i] == words[j]) dup = true;
  Assert("no duplicate between the learned pick and the fallback list", !dup);
  decoder->SetUserTable(std::wstring());
  std::remove(path);
}

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "data/zuxia.decoder.tsv";
  wchar_t wide_path[512] = {};
  for (size_t i = 0; path[i] && i < 511; ++i)
    wide_path[i] = static_cast<wchar_t>(static_cast<unsigned char>(path[i]));
  zuxia::ColumnarDecoder decoder;
  if (!decoder.Load(wide_path)) {
    std::printf("cannot load %s (run from the repo root)\n", path);
    return 2;
  }

  Check("suyaoszcw", "\xe8\x8b\x8f\xe7\x91\xb6", 3, &decoder);          // 苏瑶
  Check("suyaoszcwby", "\xe8\x8b\x8f\xe7\x91\xb6", 1, &decoder);        // 苏瑶
  // 下 是独体字（GF 0013-2009），结构位是 d 不是 s。改对之后这一档只剩一个
  // 答案 —— 独体比上下窄得多 —— 所以这里要的是 1 不是 3。上一档还是满的。
  Check("zuxiasd", "\xe8\xb6\xb3\xe4\xb8\x8b", 3, &decoder);            // 足下
  Check("zuxiasdk", "\xe8\xb6\xb3\xe4\xb8\x8b", 1, &decoder);           // 足下
  Check("yangzhipengzszmsp", "\xe6\x9d\xa8\xe5\xbf\x97\xe9\xb9\x8f", 3, &decoder);   // 杨志鹏
  Check("yangzhipengzszmspyxn", "\xe6\x9d\xa8\xe5\xbf\x97\xe9\xb9\x8f", 3, &decoder); // 杨志鹏
  Check("qingzs", "\xe6\xb8\x85", 1, &decoder);                          // 清
  Check("yingbg", "\xe5\xba\x94", 1, &decoder);                          // 应
  // 入 同样是独体字；它以前落在兜底档 p，是那 214 个错判之一。
  Check("rud", "\xe5\x85\xa5", 1, &decoder);                             // 入
  Check("rudr", "\xe5\x85\xa5", 1, &decoder);                            // 入
  CheckRejected("suyaoxx", &decoder);
  CheckRejected("suyaozz9", &decoder);
  // 「码打得满」压过「字多」：选词把结构列填满了，选此只是碰巧也解得通。
  Check("xuancibz", "\xe9\x80\x89\xe8\xaf\x8d", 3, &decoder);       // 选词

  std::printf("\nfallback to the longest valid prefix:\n");
  // 尾巴 h 没用上，交出去让输入法接着组字。
  CheckFallback("zuxiasdkh", "\xe8\xb6\xb3\xe4\xb8\x8b", "h", &decoder);
  CheckFallback("henmazzrm", "\xe5\xbe\x88\xe5\x90\x97", "rm", &decoder);
  // 退到底也拼不出来就老实交白卷，不能硬凑。
  CheckFallback("qqqqq", "", "", &decoder);
  // 整串本来就解得通的时候不许有尾巴 —— 有尾巴就等于凭空吃掉了几位码。
  CheckFallback("suyaoszcw", "\xe8\x8b\x8f\xe7\x91\xb6", "", &decoder);

  CheckRecall(&decoder);

  std::printf("\n%d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
