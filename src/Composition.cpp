#include "Globals.h"
#include "Diagnostics.h"
#include "TextService.h"

STDMETHODIMP CTextService::OnCompositionTerminated(
    TfEditCookie /*write_cookie*/,
    ITfComposition* composition) ZUXIA_COM_GUARD_BEGIN
  // 这个回调只该处理「应用把当前这份组字终止掉了」这一种情况。
  //
  // 我们自己调 _TerminateComposition 时，它已经先把 _pComposition 置空了，
  // 所以那条路走到这里看到的是空 —— 必须原样退出。原先这里无条件
  // engine_.Clear()，于是落字那一帧刚刚喂回引擎的尾巴按键（选了死码兜底
  // 候选之后剩下的那一两位）在同一帧里被清掉，用户真按过的键凭空消失。
  //
  // 另外 composition 也可能是一份不是我们正在用的组字（上下文切换过），
  // 那种情况同样不该动当前状态。
  if (!_pComposition) return S_OK;
  if (composition && composition != _pComposition) return S_OK;
  _pComposition->Release();
  _pComposition = nullptr;
  engine_.Clear();
  _HideCandidateWindow();
  return S_OK;
ZUXIA_COM_GUARD_END(L"CTextService::OnCompositionTerminated", S_OK)

BOOL CTextService::_IsComposing() const { return _pComposition != nullptr; }

void CTextService::_SetComposition(ITfComposition* composition) {
  _pComposition = composition;
}
