/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMVideoCallProfile.h"
#include "nsPIDOMWindow.h"

using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(DOMVideoCallProfile, mWindow)

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMVideoCallProfile)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMVideoCallProfile)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMVideoCallProfile)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIVideoCallProfile)
NS_INTERFACE_MAP_END

DOMVideoCallProfile::DOMVideoCallProfile(nsPIDOMWindowInner* aWindow)
  : mWindow(aWindow)
  , mQuality(VideoCallQuality::Unknown)
  , mState(VideoCallState::Audio_only)
{
}

DOMVideoCallProfile::DOMVideoCallProfile(uint16_t aQuality, uint16_t aState)
  : mWindow(nullptr)
  , mQuality(static_cast<VideoCallQuality>(aQuality))
  , mState(static_cast<VideoCallState>(aState))
{
}

void
DOMVideoCallProfile::Update(nsIVideoCallProfile* aProfile)
{
  uint16_t quality;
  uint16_t state;
  aProfile->GetQuality(&quality);
  aProfile->GetState(&state);

  mQuality = static_cast<VideoCallQuality>(quality);
  mState = static_cast<VideoCallState>(state);
}

JSObject*
DOMVideoCallProfile::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return VideoCallProfileBinding::Wrap(aCx, this, aGivenProto);
}

// nsIVideoCallProfile

NS_IMETHODIMP
DOMVideoCallProfile::GetQuality(uint16_t *aQuality)
{
  *aQuality = static_cast<uint16_t>(mQuality);
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProfile::GetState(uint16_t *aState)
{
  *aState = static_cast<uint16_t>(mState);
  return NS_OK;
}