/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/*
 * Copyright (C) 2016 Acadine Technologies. All rights reserved.
 */

#ifndef mozilla_dom_bluetooth_BluetoothConnectionHandle_h
#define mozilla_dom_bluetooth_BluetoothConnectionHandle_h

#include "BluetoothCommon.h"
#include "mozilla/dom/DOMRequest.h"
#include "nsPIDOMWindow.h"
#include "nsWrapperCache.h"

namespace mozilla {
  class ErrorResult;
  namespace dom {
    class DOMRequest;
  }
}

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothConnectionHandle final : public nsISupports
                                      , public nsWrapperCache
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(BluetoothConnectionHandle)

  static already_AddRefed<BluetoothConnectionHandle>
    Create(nsPIDOMWindowInner* aOwner, uint16_t aServiceUuid);

  nsPIDOMWindowInner* GetParentObject() const
  {
    return mOwner;
  }

  uint16_t GetServiceUuid() const
  {
    return mServiceUuid;
  }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  // Accept bluetooth profile connection request
  already_AddRefed<DOMRequest> Accept(ErrorResult& aRv);

  // Reject bluetooth profile connection request
  already_AddRefed<DOMRequest> Reject(ErrorResult& aRv);

private:
  BluetoothConnectionHandle(nsPIDOMWindowInner* aOwner, uint16_t aServiceUuid);
  ~BluetoothConnectionHandle();

  nsCOMPtr<nsPIDOMWindowInner> mOwner;
  uint16_t                mServiceUuid;
};

END_BLUETOOTH_NAMESPACE

#endif // mozilla_dom_bluetooth_BluetoothConnectionHandle
