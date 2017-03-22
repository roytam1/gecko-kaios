/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_domvideocallprovider_h__
#define mozilla_dom_domvideocallprovider_h__

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/VideoCallProviderBinding.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIVideoCallCallback.h"
#include "nsIVideoCallProvider.h"

class nsPIDOMWindowInner;

namespace mozilla {

class ErrorResult;
class nsDOMCameraControl;
class nsDOMSurfaceControl;

namespace dom {

struct SurfaceConfiguration;
struct SurfaceSize;
class Promise;
class SurfaceControlBack;

class DOMVideoCallProvider final : public DOMEventTargetHelper
                                 , private nsIVideoCallCallback
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIVIDEOCALLCALLBACK
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(DOMVideoCallProvider, DOMEventTargetHelper)
  NS_REALLY_FORWARD_NSIDOMEVENTTARGET(DOMEventTargetHelper)

  DOMVideoCallProvider(nsPIDOMWindowInner *aWindow, nsIVideoCallProvider *aHandler);

  virtual void
  DisconnectFromOwner() override;

  // WrapperCache
  virtual JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  // WebIDL APIs
  already_AddRefed<Promise>
  SetCamera(const Optional<nsAString>& aCamera, ErrorResult& aRv);

  already_AddRefed<Promise>
  GetPreviewStream(const mozilla::dom::SurfaceConfiguration& aOptions, ErrorResult& aRv);

  already_AddRefed<Promise>
  GetDisplayStream(const mozilla::dom::SurfaceConfiguration& aOptions, ErrorResult& aRv);

  already_AddRefed<Promise>
  SetOrientation(uint16_t aOrientation, ErrorResult& aRv);

  already_AddRefed<Promise>
  SetZoom(float aZoom, ErrorResult& aRv);

  already_AddRefed<Promise>
  SendSessionModifyRequest(DOMVideoCallProfile& aFrom,
                           DOMVideoCallProfile& aTo,
                           ErrorResult& aRv);

  already_AddRefed<Promise>
  SendSessionModifyResponse(DOMVideoCallProfile& aResponse, ErrorResult& aRv);

  already_AddRefed<Promise>
  RequestCameraCapabilities(ErrorResult& aRv);

  IMPL_EVENT_HANDLER(sessionmodifyrequest)
  IMPL_EVENT_HANDLER(sessionmodifyresponse)
  IMPL_EVENT_HANDLER(callsessionevent)
  IMPL_EVENT_HANDLER(changepeerdimensions)
  IMPL_EVENT_HANDLER(changecameracapabilities)
  IMPL_EVENT_HANDLER(changevideoquality)

  // Class API
  void
  SetPreviewSurface(android::sp<android::IGraphicBufferProducer>& aProducer);
  void
  SetDisplaySurface(android::sp<android::IGraphicBufferProducer>& aProducer);
  bool
  IsValidSurfaceSize(const mozilla::dom::SurfaceSize& aSize);
  bool
  IsValidSurfaceSize(const uint32_t aWidth, uint32_t aHeight);

  void Shutdown();

private:
  ~DOMVideoCallProvider();

  nsresult
  DispatchSessionModifyRequestEvent(const nsAString& aType, DOMVideoCallProfile* aRequest);

  nsresult
  DispatchSessionModifyResponseEvent(const nsAString& aType,
                                     const uint16_t aStatus,
                                     DOMVideoCallProfile* aRequest,
                                     DOMVideoCallProfile* aResponse);

  nsresult
  DispatCallSessionEvent(const nsAString& aType, const int16_t aEvent);

  nsresult
  DispatchChangePeerDimensionsEvent(const nsAString& aType, const uint16_t aWidth, const uint16_t aHeight);

  /**
   * TODO
   */
  nsresult
  DispatchCameraCapabilitiesEvent(const nsAString& aType);

  nsresult
  DispatchVideoQualityChangeEvent(const nsAString& aType, const VideoCallQuality aQuality);

  uint32_t mClientId;
  uint32_t mCallIndex;
  nsString mcamera;
  uint16_t mOrientation;
  float mZoom;

  RefPtr<nsIVideoCallProvider> mProvider;
  RefPtr<nsDOMSurfaceControl> mDisplayControl;
  RefPtr<nsDOMSurfaceControl> mPreviewControl;
  SurfaceControlBack* mDisplayCallback;
  SurfaceControlBack* mPreviewCallback;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_domvideocallprovider_h__
