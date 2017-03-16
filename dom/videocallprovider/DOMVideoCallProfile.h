/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_domvideocallprofile_h__
#define mozilla_dom_domvideocallprofile_h__

#include "mozilla/dom/VideoCallProviderBinding.h"
#include "nsIVideoCallProvider.h"
#include "nsWrapperCache.h"

class nsPIDOMWindowInner;

namespace mozilla {
namespace dom {

class DOMVideoCallProfile final : public nsIVideoCallProfile
                                , public nsWrapperCache
{
public:
  NS_DECL_NSIVIDEOCALLPROFILE
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMVideoCallProfile)

  explicit DOMVideoCallProfile(nsPIDOMWindowInner* aWindow);

  DOMVideoCallProfile(uint16_t aQuality, uint16_t aState);

  void
  Update(nsIVideoCallProfile* aProfile);

  nsPIDOMWindowInner*
  GetParentObject() const
  {
    return mWindow;
  }

  virtual JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  // WebIDL interface
  VideoCallQuality
  Quality() const
  {
    return mQuality;
  }

  VideoCallState
  State() const
  {
    return mState;
  }

private:
  ~DOMVideoCallProfile() {}

private:
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  VideoCallQuality mQuality;
  VideoCallState mState;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_domvideocallprofile_h__
