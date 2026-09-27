#include "Globals.h"
#include "EditSession.h"
#include "TextService.h"

#include <new>

class CEndCompositionEditSession final : public CEditSessionBase {
 public:
  CEndCompositionEditSession(CTextService* service, ITfContext* context)
      : CEditSessionBase(service, context) {}

  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    text_service_->_CancelComposition(cookie, context_);
    return S_OK;
  }
};

void CTextService::_TerminateComposition(TfEditCookie cookie,
                                         ITfContext* context) {
  if (!_pComposition) return;
  _pComposition->EndComposition(cookie);
  _pComposition->Release();
  _pComposition = nullptr;
}

void CTextService::_EndComposition(ITfContext* context) {
  if (!context) return;
  engine_.Clear();
  _HideCandidateWindow();

  auto* session = new (std::nothrow) CEndCompositionEditSession(this, context);
  if (!session) return;
  HRESULT session_result = E_FAIL;
  context->RequestEditSession(_tfClientId, session,
                              TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                              &session_result);
  session->Release();
}
