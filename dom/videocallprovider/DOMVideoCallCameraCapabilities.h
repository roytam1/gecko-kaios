/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_domvideocallcameracapabilities_h__
#define mozilla_dom_domvideocallcameracapabilities_h__

#include "mozilla/dom/VideoCallProviderBinding.h"
#include "nsIVideoCallProvider.h"
#include "nsIVideoCallCallback.h"
#include "nsWrapperCache.h"

class nsPIDOMWindowInner;

namespace mozilla {
namespace dom {

class DOMVideoCallCameraCapabilities final : public nsIVideoCallCameraCapabilities
                                           , public nsWrapperCache
{
public:
  NS_DECL_NSIVIDEOCALLCAMERACAPABILITIES
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMVideoCallCameraCapabilities)

  explicit DOMVideoCallCameraCapabilities(nsPIDOMWindowInner* aWindow);

  DOMVideoCallCameraCapabilities(uint16_t aWidth, uint16_t aHeight, bool aZoomSupported, uint16_t aMaxZoom);

  void
  Update(nsIVideoCallCameraCapabilities* aCapabilities);

  nsPIDOMWindowInner*
  GetParentObject() const
  {
    return mWindow;
  }

  virtual JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  // WebIDL interface
  uint16_t
  Width() const
  {
    return mWidth;
  }

  uint16_t
  Height() const
  {
    return mHeight;
  }

  bool
  ZoomSupported() const
  {
    return mZoomSupported;
  }

  uint16_t
  MaxZoom() const
  {
    return mMaxZoom;
  }

private:
  ~DOMVideoCallCameraCapabilities() {}

private:
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  uint16_t mWidth;
  uint16_t mHeight;
  bool mZoomSupported;
  uint16_t mMaxZoom;
};

} // dom
}  // mozilla

#endif // mozilla_dom_domvideocallcameracapabilities_h__