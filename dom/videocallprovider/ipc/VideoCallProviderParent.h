/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_videocallprovider_VideoCallProviderParent_h__
#define mozilla_dom_videocallprovider_VideoCallProviderParent_h__

#include "mozilla/dom/videocallprovider/PVideoCallProviderParent.h"
#include "nsIVideoCallProvider.h"
#include "nsIVideoCallCallback.h"

namespace mozilla {
namespace dom {
namespace videocallprovider {

class VideoCallProviderParent : public PVideoCallProviderParent
                              , public nsIVideoCallCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIVIDEOCALLCALLBACK

  explicit VideoCallProviderParent(uint32_t aClientId, uint32_t aCallIndex);

protected:
  virtual
  ~VideoCallProviderParent()
  {
    MOZ_COUNT_DTOR(ImsRegistrationParent);
  }

  virtual void
  ActorDestroy(ActorDestroyReason aWhy) override;

  virtual bool
  RecvSetCamera(const int16_t& cameraId) override;

  virtual bool
  RecvSetPreviewSurface() override;

  virtual bool
  RecvSetDisplaySurface() override;

  virtual bool
  RecvSetDeviceOrientation(const uint16_t& aOrientation) override;

  virtual bool
  RecvSetZoom(const float& aValue) override;

  virtual bool
  RecvSendSessionModifyRequest(const nsVideoCallProfile& aFromProfile,
                               const nsVideoCallProfile& aToProfile) override;

  virtual bool
  RecvSendSessionModifyResponse(const nsVideoCallProfile& aResponse) override;

  virtual bool
  RecvRequestCameraCapabilities() override;

private:
  uint32_t mClientId;
  uint32_t mCallIndex;
  nsCOMPtr<nsIVideoCallProvider> mProvider;
};

} // videocallprovider
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_videocallprovider_VideoCallProviderParent_h__