// Standalone smoke probe: loads rime.dll exactly like RimeEngine does and
// replays the key acceptance cases from the engineering plan (section 10.3).
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "rime_api.h"

static RimeApi* api = nullptr;
static const char* g_schema = "zuxia";

static void PrintUtf8(const char* s) {
  if (!s) { printf("(null)"); return; }
  printf("%s", s);
}

static void RunCase(const char* keys, int show) {
  RimeSessionId s = api->create_session();
  if (!s) { printf("  !! create_session failed\n"); return; }
  if (!api->select_schema(s, g_schema)) {
    printf("  !! select_schema(%s) failed\n", g_schema);
    api->destroy_session(s);
    return;
  }
  api->set_option(s, "ascii_mode", False);
  for (const char* p = keys; *p; ++p) {
    api->process_key(s, (int)(unsigned char)*p, 0);
  }
  RIME_STRUCT(RimeContext, ctx);
  if (!api->get_context(s, &ctx)) {
    printf("  !! get_context failed\n");
    api->destroy_session(s);
    return;
  }
  printf("  input   : %s\n", keys);
  printf("  preedit : ");
  PrintUtf8(ctx.composition.preedit);
  printf("\n  cands(%d):", ctx.menu.num_candidates);
  int n = ctx.menu.num_candidates < show ? ctx.menu.num_candidates : show;
  for (int i = 0; i < n; ++i) {
    printf(" [%d]", i + 1);
    PrintUtf8(ctx.menu.candidates[i].text);
    const char* note = ctx.menu.candidates[i].comment;
    if (note && *note) {
      printf("<");
      PrintUtf8(note);
      printf(">");
    }
  }
  printf("\n");
  api->free_context(&ctx);
  api->destroy_session(s);
}

int main(int argc, char** argv) {
  if (argc < 3) { printf("usage: probe <dllpath> <shared> <user>\n"); return 2; }
  SetConsoleOutputCP(CP_UTF8);
  HMODULE m = LoadLibraryA(argv[1]);
  if (!m) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
  typedef RimeApi* (__cdecl *GetApi)();
  GetApi g = (GetApi)GetProcAddress(m, "rime_get_api");
  if (!g) { printf("rime_get_api missing\n"); return 1; }
  api = g();
  if (!api) { printf("api null\n"); return 1; }

  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = argv[2];
  traits.user_data_dir = argv[3];
  traits.distribution_name = "Hengma";
  traits.distribution_code_name = "hengma";
  traits.distribution_version = "0.3.0-alpha";
  traits.app_name = "rime.hengma.probe";
  g_schema = argv[4];
  api->setup(&traits);
  api->initialize(&traits);
  printf("deploying (first run compiles dictionaries, this takes a while)...\n");
  fflush(stdout);
  DWORD t0 = GetTickCount();
  Bool maint = api->start_maintenance(True);
  if (maint) api->join_maintenance_thread();
  printf("deploy done in %.1f s\n\n", (GetTickCount() - t0) / 1000.0);

  // Every remaining argument is a code to replay through the engine.
  for (int i = 5; i < argc; ++i) {
    RunCase(argv[i], 6);
  }
  api->finalize();
  return 0;
}
