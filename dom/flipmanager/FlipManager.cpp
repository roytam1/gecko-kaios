/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FlipManager.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/Hal.h"
#include "mozilla/dom/FlipManagerBinding.h"
#include "nsIDOMClassInfo.h"

namespace mozilla {
namespace dom {

NS_IMPL_ADDREF_INHERITED(FlipManager, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(FlipManager, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(FlipManager)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

FlipManager::FlipManager(nsPIDOMWindowInner* aWindow)
  : DOMEventTargetHelper(aWindow)
{
}

FlipManager::~FlipManager()
{
}

void
FlipManager::Init()
{
  hal::RegisterFlipObserver(this);

  bool flipStatus = hal::IsFlipOpened();
  SetFlipStatus(flipStatus);
}

void
FlipManager::Shutdown()
{
  hal::UnregisterFlipObserver(this);
}

JSObject*
FlipManager::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return FlipManagerBinding::Wrap(aCx, this, aGivenProto);
}

void
FlipManager::Notify(const bool& aIsOpened)
{
  if (aIsOpened != mFlipOpened) {
    SetFlipStatus(aIsOpened);
    DispatchTrustedEvent(NS_LITERAL_STRING("flipchange"));
  }
}

} // namespace dom
} // namespace mozilla
