#include "Globals.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      g_hInst = instance;
      DisableThreadLibraryCalls(instance);
      if (!InitializeCriticalSectionAndSpinCount(&g_cs, 4000)) return FALSE;
      break;
    case DLL_PROCESS_DETACH:
      DeleteCriticalSection(&g_cs);
      break;
    default:
      break;
  }
  return TRUE;
}
