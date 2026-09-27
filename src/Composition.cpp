#include "Globals.h"
#include "TextService.h"

STDMETHODIMP CTextService::OnCompositionTerminated(
    TfEditCookie /*write_cookie*/, ITfComposition* composition) {
  if (_pComposition) {
    // TSF owns the callback ordering. Release only our cached reference.
    _pComposition->Release();
    _pComposition = nullptr;
  }
  engine_.Clear();
  _HideCandidateWindow();
  return S_OK;
}

BOOL CTextService::_IsComposing() const { return _pComposition != nullptr; }

void CTextService::_SetComposition(ITfComposition* composition) {
  _pComposition = composition;
}
