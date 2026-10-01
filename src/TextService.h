#pragma once

#include "Globals.h"
#include "RimeEngine.h"

class CCandidateWindow;
class CModeButton;
namespace zuxia { class CPartsWindow; }

class CTextService : public ITfTextInputProcessor,
                     public ITfThreadMgrEventSink,
                     public ITfTextEditSink,
                     public ITfKeyEventSink,
                     public ITfCompartmentEventSink,
                     public ITfCompositionSink {
 public:
  CTextService();
  ~CTextService();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfTextInputProcessor
  STDMETHODIMP Activate(ITfThreadMgr* thread_mgr,
                        TfClientId client_id) override;
  STDMETHODIMP Deactivate() override;

  // ITfThreadMgrEventSink
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* document_mgr) override;
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* document_mgr) override;
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* focused,
                          ITfDocumentMgr* previous) override;
  STDMETHODIMP OnPushContext(ITfContext* context) override;
  STDMETHODIMP OnPopContext(ITfContext* context) override;

  // ITfTextEditSink
  STDMETHODIMP OnEndEdit(ITfContext* context, TfEditCookie read_cookie,
                         ITfEditRecord* edit_record) override;

  // ITfKeyEventSink
  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM flags,
                             BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM key, LPARAM flags,
                         BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM key, LPARAM flags,
                           BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM key, LPARAM flags,
                       BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid,
                              BOOL* eaten) override;

  // ITfCompartmentEventSink
  STDMETHODIMP OnChange(REFGUID guid) override;

  // ITfCompositionSink
  STDMETHODIMP OnCompositionTerminated(TfEditCookie write_cookie,
                                        ITfComposition* composition) override;

  static HRESULT CreateInstance(IUnknown* outer, REFIID riid, void** object);

  ITfThreadMgr* _GetThreadMgr() const { return _pThreadMgr; }
  TfClientId _GetClientId() const { return _tfClientId; }

  BOOL _IsKeyboardDisabled();
  BOOL _IsKeyboardOpen();
  HRESULT _SetKeyboardOpen(BOOL open);

  bool _IsNativeMode();
  void _SetNativeMode(bool native);
  void _ToggleInputMode();
  void _ApplyInputMode();

  void _EndComposition(ITfContext* context);
  void _TerminateComposition(TfEditCookie cookie, ITfContext* context);
  BOOL _IsComposing() const;
  void _SetComposition(ITfComposition* composition);

  HRESULT _InvokeKeyHandler(ITfContext* context, WPARAM key, LPARAM flags);
  HRESULT _ApplyRimeSnapshot(TfEditCookie cookie, ITfContext* context,
                             const zuxia::EngineSnapshot& snapshot);
  void _CancelComposition(TfEditCookie cookie, ITfContext* context);

  void _HideCandidateWindow();
  // Update parts window with the highlighted candidate text (realtime).
  // Pass empty string to hide. Checks CurrentAppearance().show_parts_window.
  void _UpdatePartsWindow(const std::wstring& highlighted_text);
  bool _EngineReady() const { return engine_.Ready(); }
  bool _EngineComposing() const { return engine_.IsComposing(); }
  zuxia::RimeEngine& _Engine() { return engine_; }

 private:
  BOOL _InitThreadMgrEventSink();
  void _UninitThreadMgrEventSink();
  BOOL _InitTextEditSink(ITfDocumentMgr* document_mgr);
  BOOL _InitKeyEventSink();
  void _UninitKeyEventSink();
  BOOL _InitPreservedKey();
  void _UninitPreservedKey();
  BOOL _InitInputMode();
  void _UninitInputMode();
  BOOL _InitLanguageBar();
  void _UninitLanguageBar();
  BOOL _IsKeyEaten(ITfContext* context, WPARAM key);
  void _NoteKeyForShiftTap(WPARAM key);

  HRESULT _EnsureComposition(TfEditCookie cookie, ITfContext* context);
  HRESULT _SetCompositionText(TfEditCookie cookie, ITfContext* context,
                              const std::wstring& text);
  HRESULT _CommitText(TfEditCookie cookie, ITfContext* context,
                      const std::wstring& text);
  void _UpdateCandidateWindow(TfEditCookie cookie, ITfContext* context,
                              const zuxia::EngineSnapshot& snapshot);

  ITfThreadMgr* _pThreadMgr = nullptr;
  TfClientId _tfClientId = 0;
  DWORD _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
  ITfContext* _pTextEditSinkContext = nullptr;
  DWORD _dwTextEditSinkCookie = TF_INVALID_COOKIE;
  ITfComposition* _pComposition = nullptr;
  ITfCompartment* _pModeCompartment = nullptr;
  DWORD _dwModeSinkCookie = TF_INVALID_COOKIE;
  CCandidateWindow* candidate_window_ = nullptr;
  zuxia::CPartsWindow* parts_window_ = nullptr;
  CModeButton* lang_bar_ = nullptr;
  zuxia::RimeEngine engine_;
  bool _shiftUsedWithKey = false;
  LONG _cRef = 1;
};
