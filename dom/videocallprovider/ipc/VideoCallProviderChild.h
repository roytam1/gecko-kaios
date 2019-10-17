/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_videocallprovider_VideoCallProviderChild_h__
#define mozilla_dom_videocallprovider_VideoCallProviderChild_h__

#include "mozilla/dom/videocallprovider/PVideoCallProviderChild.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsIVideoCallProvider.h"
#include "nsIVideoCallCallback.h"

namespace mozilla {
namespace dom {
namespace videocallprovider {

class VideoCallProviderChild : public PVideoCallProviderChild
                             , public nsIVideoCallProvider
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIVIDEOCALLPROVIDER

  explicit VideoCallProviderChild(uint32_t aClientId, uint32_t aCallIndex);

  void
  Shutdown();

protected:
  virtual void
  ActorDestroy(ActorDestroyReason aWhy) override;

  virtual bool
  RecvNotifyReceiveSessionModifyRequest(nsIVideoCallProfile* const& aRequest) override;


  virtual bool
  RecvNotifyReceiveSessionModifyResponse(const uint16_t& aStatus,
                                         nsIVideoCallProfile* const& aRequest,
                                         nsIVideoCallProfile* const& aResponse) override;

  virtual bool
  RecvNotifyHandleCallSessionEvent(const uint16_t& aEvent) override;

  virtual bool
  RecvNotifyChangePeerDimensions(const uint16_t& aWidth, const uint16_t& aHeight) override;

  virtual bool
  RecvNotifyChangeCameraCapabilities(nsIVideoCallCameraCapabilities* const& aCapabilities) override;

  virtual bool
  RecvNotifyChangeVideoQuality(const uint16_t& aQuality) override;

private:
  VideoCallProviderChild() = delete;

  ~VideoCallProviderChild()
  {
    MOZ_COUNT_DTOR(VideoCallProviderChild);
  }

  bool mLive;
  uint32_t mClientId;
  uint32_t mCallIndex;
  nsCOMArray<nsIVideoCallCallback> mCallbacks;
};

} // namespace videocallprovider
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_videocallprovider_VideoCallProviderChild_h__