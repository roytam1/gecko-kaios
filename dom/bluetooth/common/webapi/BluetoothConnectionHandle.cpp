/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/*
 * Copyright (C) 2016 Acadine Technologies. All rights reserved.
 */

#include "BluetoothConnectionHandle.h"
#include "BluetoothDevice.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"

#include "mozilla/dom/BluetoothConnectionHandleBinding.h"

using namespace mozilla;
using namespace dom;

USING_BLUETOOTH_NAMESPACE

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(BluetoothConnectionHandle, mOwner)
NS_IMPL_CYCLE_COLLECTING_ADDREF(BluetoothConnectionHandle)
NS_IMPL_CYCLE_COLLECTING_RELEASE(BluetoothConnectionHandle)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BluetoothConnectionHandle)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

BluetoothConnectionHandle::BluetoothConnectionHandle(nsPIDOMWindowInner* aOwner,
                                                     uint16_t aServiceUuid)
  : mOwner(aOwner)
  , mServiceUuid(aServiceUuid)
{
  MOZ_ASSERT(aOwner);
}

BluetoothConnectionHandle::~BluetoothConnectionHandle()
{
}

already_AddRefed<BluetoothConnectionHandle>
BluetoothConnectionHandle::Create(nsPIDOMWindowInner* aOwner,
                                  uint16_t aServiceUuid)
{
  MOZ_ASSERT(aOwner);

  RefPtr<BluetoothConnectionHandle> handle =
    new BluetoothConnectionHandle(aOwner, aServiceUuid);

  return handle.forget();
}

already_AddRefed<DOMRequest>
BluetoothConnectionHandle::Accept(ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindowInner> win = GetParentObject();
  if (!win) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<DOMRequest> request = new DOMRequest(win);
  RefPtr<BluetoothVoidReplyRunnable> result =
    new BluetoothVoidReplyRunnable(request);

  bs->AcceptConnection(mServiceUuid, result);

  return request.forget();
}

already_AddRefed<DOMRequest>
BluetoothConnectionHandle::Reject(ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindowInner> win = GetParentObject();
  if (!win) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<DOMRequest> request = new DOMRequest(win);
  RefPtr<BluetoothVoidReplyRunnable> result =
    new BluetoothVoidReplyRunnable(request);

  bs->RejectConnection(mServiceUuid, result);

  return request.forget();
}

JSObject*
BluetoothConnectionHandle::WrapObject(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto)
{
  return BluetoothConnectionHandleBinding::Wrap(aCx, this, aGivenProto);
}
