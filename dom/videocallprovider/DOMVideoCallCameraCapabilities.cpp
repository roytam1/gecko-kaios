/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMVideoCallCameraCapabilities.h"
#include "nsPIDOMWindow.h"

using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(DOMVideoCallCameraCapabilities, mWindow)

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMVideoCallCameraCapabilities)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMVideoCallCameraCapabilities)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMVideoCallCameraCapabilities)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIVideoCallCameraCapabilities)
NS_INTERFACE_MAP_END

DOMVideoCallCameraCapabilities::DOMVideoCallCameraCapabilities(nsPIDOMWindowInner* aWindow)
  : mWindow(aWindow)
  , mWidth(0)
  , mHeight(0)
  , mZoomSupported(false)
  , mMaxZoom(0)
{
}

DOMVideoCallCameraCapabilities::DOMVideoCallCameraCapabilities(uint16_t aWidth, uint16_t aHeight, bool aZoomSupported,
                                                               uint16_t aMaxZoom)
  : mWindow(nullptr)
  , mWidth(aWidth)
  , mHeight(aHeight)
  , mZoomSupported(aZoomSupported)
  , mMaxZoom(aMaxZoom)
{
}

void
DOMVideoCallCameraCapabilities::Update(nsIVideoCallCameraCapabilities* aCapabilities)
{
  aCapabilities->GetWidth(&mWidth);
  aCapabilities->GetHeight(&mHeight);
  aCapabilities->GetZoomSupported(&mZoomSupported);
  aCapabilities->GetMaxZoom(&mMaxZoom);
}

JSObject*
DOMVideoCallCameraCapabilities::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return VideoCallCameraCapabilitiesBinding::Wrap(aCx, this, aGivenProto);
}

// nsIVideoCallCameraCapabilities

NS_IMETHODIMP
DOMVideoCallCameraCapabilities::GetWidth(uint16_t *aWidth)
{
  *aWidth = mWidth;
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallCameraCapabilities::GetHeight(uint16_t *aHeight)
{
  *aHeight = mHeight;
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallCameraCapabilities::GetZoomSupported(bool *aZoomSupported)
{
  *aZoomSupported = mZoomSupported;
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallCameraCapabilities::GetMaxZoom(uint16_t *aMaxZoom)
{
  *aMaxZoom = mMaxZoom;
  return NS_OK;
}