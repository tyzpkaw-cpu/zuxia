#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <msctf.h>
#include <strsafe.h>

#include <cassert>

void DllAddRef();
void DllRelease();

#define TEXTSERVICE_LANGID \
  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)
#define TEXTSERVICE_DESC TEXT("应物音形足下输入法")
#define TEXTSERVICE_MODEL TEXT("Apartment")
#define TEXTSERVICE_ICON_INDEX 0
#define TEXTSERVICE_ASCII_KEY_DESC TEXT("中/西文切换")

extern HINSTANCE g_hInst;
extern LONG g_cRefDll;
extern CRITICAL_SECTION g_cs;

extern const CLSID c_clsidTextService;
extern const GUID c_guidProfile;
extern const GUID c_guidToggleAsciiKey;
