// Test harness: runs the installer's own cabinet extraction into a scratch
// directory, without touching the registry or Program Files.
#include "cabextract.h"

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    wprintf(L"usage: cabtest <cab> <dest> [skip-x64]\n");
    return 2;
  }
  zx::ExtractContext context;
  context.root = argv[2];
  context.skip_x64 = argc > 3;
  if (!zx::EnsureDir(context.root)) {
    wprintf(L"cannot create %s\n", context.root.c_str());
    return 1;
  }
  DWORD error = 0;
  if (!zx::ExtractCabinet(argv[1], &context, &error)) {
    wprintf(L"extract failed, error %lu\n", error);
    return 1;
  }
  wprintf(L"files=%d bytes=%llu\n", context.files, context.bytes);
  return 0;
}
