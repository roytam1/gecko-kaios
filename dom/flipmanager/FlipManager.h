/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FlipManager_h
#define mozilla_dom_FlipManager_h

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/Observer.h"
#include "nsCycleCollectionParticipant.h"

class nsIScriptContext;

namespace mozilla {
namespace dom {

typedef Observer<bool> FlipObserver;

class FlipManager final : public DOMEventTargetHelper
                        , public FlipObserver
{
public:
  NS_DECL_ISUPPORTS_INHERITED

  explicit FlipManager(nsPIDOMWindowInner* aWindow);

  void Init();
  void Shutdown();


  void Notify(const bool& aIsOpened);

  nsPIDOMWindowInner* GetParentObject() const
  {
     return GetOwner();
  }

  virtual JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  bool FlipOpened() const
  {
    return mFlipOpened;
  }

  IMPL_EVENT_HANDLER(flipchange)

private:
  ~FlipManager();

  void SetFlipStatus(bool aFlipStatus)
  {
    mFlipOpened = aFlipStatus;
  }

  bool mFlipOpened;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_FlipManager_h
