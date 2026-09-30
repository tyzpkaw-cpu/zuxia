#include "Globals.h"
#include "EditSession.h"
#include "Diagnostics.h"
#include "TextService.h"

#include <new>

class CEndCompositionEditSession final : public CEditSessionBase {
 public:
  CEndCompositionEditSession(CTextService* service, ITfContext* context)
      : CEditSessionBase(service, context) {}

  STDMETHODIMP DoEditSession(TfEditCookie cookie) override ZUXIA_COM_GUARD_BEGIN
    text_service_->_CancelComposition(cookie, context_);
    return S_OK;
  ZUXIA_COM_GUARD_END(L"CEndCompositionEditSession::DoEditSession", E_FAIL)
};

void CTextService::_TerminateComposition(TfEditCookie cookie,
                                         ITfContext* /*context*/) {
  ITfComposition* composition = _pComposition;
  if (!composition) return;
  // 先把成员置空，再去收尾。EndComposition 允许同步回调
  // OnCompositionTerminated，那个回调会把 _pComposition 释放并置空 ——
  // 回来之后这里再 Release 一次就是二次释放，再解引用就是空指针。
  // 置空还有第二个作用：让 OnCompositionTerminated 能分辨「这次终止是我们
  // 自己发起的」，从而不去做属于「应用抢走了组字」那一路的收尾动作。
  _pComposition = nullptr;
  composition->EndComposition(cookie);
  composition->Release();
}

void CTextService::_EndComposition(ITfContext* context) {
  if (!context) return;
  engine_.Clear();
  _HideCandidateWindow();
  if (!_pComposition) return;  // TSF 这边没有组字要收

  auto* session = new (std::nothrow) CEndCompositionEditSession(this, context);
  if (!session) return;
  // 先要同步写锁。异步会话的语义是「有空再说」，TSF 完全可以一直不跑它；
  // 而这时候 librime 已经清空了，TSF 那边的组字范围还留着一串码，于是文档
  // 里留下一段谁都清不掉的死文本 —— 再按 Esc 走的还是这条路，还是清不掉。
  HRESULT session_result = E_FAIL;
  HRESULT failure = S_OK;
  HRESULT request = context->RequestEditSession(
      _tfClientId, session, TF_ES_SYNC | TF_ES_READWRITE, &session_result);
  if (SUCCEEDED(request)) {
    // 同步会话已经在 RequestEditSession 里跑完了，这个值是真的。
    failure = session_result;
  } else {
    // 拿不到同步写锁，最常见的原因是我们正被别人的编辑会话回调着
    // （OnEndEdit 就是只读会话里的回调）。这种场合只能退回异步，至少还有
    // 机会收掉。异步的结果这里拿不到，所以只看请求本身成不成。
    HRESULT queued = S_OK;
    request = context->RequestEditSession(_tfClientId, session,
                                          TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                                          &queued);
    failure = FAILED(request) ? request : S_OK;
  }
  session->Release();
  // 这两个返回值原先都被丢掉了。收不掉组字是用户能直接看见的故障（文档里
  // 留着一串清不掉的码），留个记号。
  if (FAILED(failure)) {
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 8) {
      zuxia::LogFailure(L"end-composition-failed",
                        static_cast<unsigned long>(failure));
    }
  }
}
