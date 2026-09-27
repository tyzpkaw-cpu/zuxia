#pragma once

#include "TextService.h"

class CEditSessionBase : public ITfEditSession {
 public:
  CEditSessionBase(CTextService* text_service, ITfContext* context)
      : context_(context), text_service_(text_service) {
    context_->AddRef();
    text_service_->AddRef();
  }

  virtual ~CEditSessionBase() {
    context_->Release();
    text_service_->Release();
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (!object) return E_INVALIDARG;
    *object = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfEditSession)) {
      *object = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
  }

  STDMETHODIMP_(ULONG) Release() override {
    const LONG count = InterlockedDecrement(&ref_count_);
    assert(count >= 0);
    if (count == 0) delete this;
    return static_cast<ULONG>(count);
  }

  STDMETHODIMP DoEditSession(TfEditCookie cookie) override = 0;

 protected:
  ITfContext* context_;
  CTextService* text_service_;

 private:
  LONG ref_count_ = 1;
};
