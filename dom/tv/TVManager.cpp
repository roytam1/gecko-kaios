/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Promise.h"
#include "mozilla/dom/TVManagerBinding.h"
#include "mozilla/dom/TVServiceCallbacks.h"
#include "mozilla/dom/TVServiceFactory.h"
#include "mozilla/dom/TVTuner.h"
#include "nsITVService.h"
#include "nsServiceManagerUtils.h"
#include "TVManager.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(TVManager, DOMEventTargetHelper, mTuners,
                                   mPendingGetTunersPromises)

NS_IMPL_ADDREF_INHERITED(TVManager, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(TVManager, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(TVManager)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

TVManager::TVManager(nsPIDOMWindowInner* aWindow)
  : DOMEventTargetHelper(aWindow)
  , mIsReady(false)
{
}

TVManager::~TVManager()
{
}

/* static */ already_AddRefed<TVManager>
TVManager::Create(nsPIDOMWindowInner* aWindow)
{
  RefPtr<TVManager> manager = new TVManager(aWindow);
  return (manager->Init()) ? manager.forget() : nullptr;
}

bool
TVManager::Init()
{
  nsCOMPtr<nsITVService> service = do_GetService(TV_SERVICE_CONTRACTID);
  if (NS_WARN_IF(!service)) {
    return false;
  }

  nsCOMPtr<nsITVServiceCallback> callback = new TVServiceTunerGetterCallback(this);
  nsresult rv = service->GetTuners(callback);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

/* virtual */ JSObject*
TVManager::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return TVManagerBinding::Wrap(aCx, this, aGivenProto);
}

nsresult
TVManager::SetTuners(const nsTArray<RefPtr<TVTuner>>& aTuners)
{
  // Should be called only when TV Manager hasn't been ready yet.
  if (mIsReady) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  mTuners = aTuners;
  mIsReady = true;

  // Resolve pending promises.
  uint32_t length = mPendingGetTunersPromises.Length();
  for(uint32_t i = 0; i < length; i++) {
    mPendingGetTunersPromises[i]->MaybeResolve(mTuners);
  }
  mPendingGetTunersPromises.Clear();
  return NS_OK;
}

void
TVManager::RejectPendingGetTunersPromises(nsresult aRv)
{
  // Reject pending promises.
  uint32_t length = mPendingGetTunersPromises.Length();
  for(uint32_t i = 0; i < length; i++) {
    mPendingGetTunersPromises[i]->MaybeReject(aRv);
  }
  mPendingGetTunersPromises.Clear();
}

nsresult
TVManager::DispatchTVEvent(nsIDOMEvent* aEvent)
{
  return DispatchTrustedEvent(aEvent);
}

already_AddRefed<Promise>
TVManager::GetTuners(ErrorResult& aRv)
{
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  MOZ_ASSERT(global);

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  // Keep track of the promise when the manager hasn't been ready yet.
  if (mIsReady) {
    promise->MaybeResolve(mTuners);
  } else {
    mPendingGetTunersPromises.AppendElement(promise);
  }

  return promise.forget();
}

} // namespace dom
} // namespace mozilla
