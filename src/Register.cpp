#include "Globals.h"

#include <string>

namespace {

bool GuidString(REFGUID guid, wchar_t (&buffer)[40]) {
  return StringFromGUID2(guid, buffer, ARRAYSIZE(buffer)) > 0;
}

bool SetStringValue(HKEY key, const wchar_t* name, const wchar_t* value) {
  const DWORD bytes =
      static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
  return RegSetValueExW(key, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value), bytes) ==
         ERROR_SUCCESS;
}

}  // namespace

BOOL RegisterProfiles() {
  ITfInputProcessorProfiles* profiles = nullptr;
  HRESULT result = CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
      IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
  if (FAILED(result) || !profiles) return FALSE;

  result = profiles->Register(c_clsidTextService);
  if (FAILED(result)) {
    profiles->Release();
    return FALSE;
  }

  wchar_t module[MAX_PATH] = {};
  const DWORD module_length =
      GetModuleFileNameW(g_hInst, module, ARRAYSIZE(module));
  if (!module_length || module_length >= ARRAYSIZE(module)) {
    profiles->Release();
    return FALSE;
  }

  // Makes repeated x64/x86 registration deterministic.
  profiles->RemoveLanguageProfile(c_clsidTextService, TEXTSERVICE_LANGID,
                                  c_guidProfile);
  result = profiles->AddLanguageProfile(
      c_clsidTextService, TEXTSERVICE_LANGID, c_guidProfile, TEXTSERVICE_DESC,
      static_cast<ULONG>(wcslen(TEXTSERVICE_DESC)), module, module_length,
      TEXTSERVICE_ICON_INDEX);
  if (SUCCEEDED(result)) {
    // Best effort: expose the profile to the current user immediately.
    profiles->EnableLanguageProfile(c_clsidTextService, TEXTSERVICE_LANGID,
                                    c_guidProfile, TRUE);
  }
  profiles->Release();
  return SUCCEEDED(result);
}

void UnregisterProfiles() {
  ITfInputProcessorProfiles* profiles = nullptr;
  if (SUCCEEDED(CoCreateInstance(
          CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
          IID_ITfInputProcessorProfiles,
          reinterpret_cast<void**>(&profiles))) &&
      profiles) {
    profiles->RemoveLanguageProfile(c_clsidTextService, TEXTSERVICE_LANGID,
                                    c_guidProfile);
    profiles->Unregister(c_clsidTextService);
    profiles->Release();
  }
}

BOOL RegisterCategories() {
  ITfCategoryMgr* manager = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                              CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                              reinterpret_cast<void**>(&manager))) ||
      !manager) {
    return FALSE;
  }

  const HRESULT keyboard = manager->RegisterCategory(
      c_clsidTextService, GUID_TFCAT_TIP_KEYBOARD, c_clsidTextService);
  manager->Release();
  return SUCCEEDED(keyboard);
}

void UnregisterCategories() {
  ITfCategoryMgr* manager = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                 reinterpret_cast<void**>(&manager))) &&
      manager) {
    manager->UnregisterCategory(c_clsidTextService, GUID_TFCAT_TIP_KEYBOARD,
                                c_clsidTextService);
    manager->Release();
  }
}

BOOL RegisterServer() {
  wchar_t guid[40] = {};
  if (!GuidString(c_clsidTextService, guid)) return FALSE;
  const std::wstring clsid_key = std::wstring(L"CLSID\\") + guid;

  HKEY key = nullptr;
  DWORD disposition = 0;
  if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsid_key.c_str(), 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
                      &disposition) != ERROR_SUCCESS) {
    return FALSE;
  }

  bool ok = SetStringValue(key, nullptr, TEXTSERVICE_DESC);
  HKEY inproc = nullptr;
  if (ok && RegCreateKeyExW(key, L"InProcServer32", 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                            &inproc, &disposition) == ERROR_SUCCESS) {
    wchar_t module[MAX_PATH] = {};
    const DWORD length =
        GetModuleFileNameW(g_hInst, module, ARRAYSIZE(module));
    ok = length > 0 && length < ARRAYSIZE(module) &&
         SetStringValue(inproc, nullptr, module) &&
         SetStringValue(inproc, L"ThreadingModel", TEXTSERVICE_MODEL);
    RegCloseKey(inproc);
  } else {
    ok = false;
  }
  RegCloseKey(key);
  return ok ? TRUE : FALSE;
}

void UnregisterServer() {
  wchar_t guid[40] = {};
  if (!GuidString(c_clsidTextService, guid)) return;
  const std::wstring clsid_key = std::wstring(L"CLSID\\") + guid;
  RegDeleteTreeW(HKEY_CLASSES_ROOT, clsid_key.c_str());
}
