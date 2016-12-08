/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FlipManager.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/Hal.h"
#include "mozilla/dom/FlipManagerBinding.h"
#include "mozilla/dom/Promise.h"
#include "nsIDOMClassInfo.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(FlipManager, DOMEventTargetHelper,
                                   mPendingFlipPromises)

NS_IMPL_ADDREF_INHERITED(FlipManager, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(FlipManager, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(FlipManager)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

FlipManager::FlipManager(nsPIDOMWindowInner* aWindow)
  : DOMEventTargetHelper(aWindow)
  , mFlipOpened(false)
{
}

FlipManager::~FlipManager()
{
}

void
FlipManager::Init()
{
  hal::RegisterFlipObserver(this);
}

void
FlipManager::Shutdown()
{
  hal::UnregisterFlipObserver(this);

  for (uint32_t i = 0; i < mPendingFlipPromises.Length(); ++i) {
    mPendingFlipPromises[i]->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
  }
  mPendingFlipPromises.Clear();
}

JSObject*
FlipManager::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return FlipManagerBinding::Wrap(aCx, this, aGivenProto);
}

void
FlipManager::Notify(const bool& aIsOpened)
{
  bool hasChanged = aIsOpened != mFlipOpened;
  mFlipOpened = aIsOpened;

  for (uint32_t i = 0; i < mPendingFlipPromises.Length(); ++i) {
    mPendingFlipPromises[i]->MaybeResolve(this);
  }
  mPendingFlipPromises.Clear();

  if (hasChanged) {
    DispatchTrustedEvent(NS_LITERAL_STRING("flipchange"));
  }
}

already_AddRefed<Promise>
FlipManager::GetPromise(ErrorResult& aRv)
{
  nsCOMPtr<nsIGlobalObject> go = do_QueryInterface(GetOwner());
  if (!go) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(go, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  mPendingFlipPromises.AppendElement(promise);
  hal::RequestCurrentFlipState();

  return promise.forget();
}

} // namespace dom
} // namespace mozilla
