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

HANDLE CreateFileW(LPCWSTR name, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
  FILE* file = std::fopen(Narrow(name).c_str(), "rb");
  return file ? reinterpret_cast<HANDLE>(file) : INVALID_HANDLE_VALUE;
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
  Check("zuxiassk", "\xe8\xb6\xb3\xe4\xb8\x8b", 3, &decoder);           // 足下
  Check("yangzhipengzszmsp", "\xe6\x9d\xa8\xe5\xbf\x97\xe9\xb9\x8f", 3, &decoder);   // 杨志鹏
  Check("yangzhipengzszmspyxn", "\xe6\x9d\xa8\xe5\xbf\x97\xe9\xb9\x8f", 3, &decoder); // 杨志鹏
  Check("qingzs", "\xe6\xb8\x85", 1, &decoder);                          // 清
  Check("yingbg", "\xe5\xba\x94", 1, &decoder);                          // 应
  Check("rup", "\xe5\x85\xa5", 1, &decoder);                             // 入
  Check("rupr", "\xe5\x85\xa5", 1, &decoder);                            // 入
  Check("suyaoxx", "", 0, &decoder);
  Check("suyaozz9", "", 0, &decoder);

  std::printf("\n%d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
