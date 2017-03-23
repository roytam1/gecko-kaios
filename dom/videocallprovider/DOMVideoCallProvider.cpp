/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMSurfaceControl.h"
#include "IDOMSurfaceControlCallback.h"
#include "mozilla/dom/DOMVideoCallProvider.h"
#include "nsIVideoCallProvider.h"
#include "nsPIDOMWindow.h"
#include "mozilla/dom/VideoCallCameraCapabilitiesChangeEvent.h"
#include "mozilla/dom/VideoCallPeerDimensionsEvent.h"
#include "mozilla/dom/VideoCallQualityEvent.h"
#include "mozilla/dom/VideoCallSessionChangeEvent.h"
#include "mozilla/dom/VideoCallSessionModifyRequestEvent.h"
#include "mozilla/dom/VideoCallSessionModifyResponseEvent.h"

#include <android/log.h>
#undef LOG
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "DOMVideoCallProvider" , ## args)

using mozilla::ErrorResult;

namespace mozilla {
namespace dom {

const int16_t TYPE_DISPLAY = 0;
const int16_t TYPE_PREVIEW = 1;

class SurfaceControlBack : public IDOMSurfaceControlCallback
{
public:
  SurfaceControlBack(RefPtr<DOMVideoCallProvider> aProvider, int16_t aType,
      const SurfaceConfiguration& aConfig);
  ~SurfaceControlBack() { }
  virtual void OnProducerCreated(
    android::sp<android::IGraphicBufferProducer> aProducer) override;
  virtual void OnProducerDestroyed() override;
  void Shutdown();

private:
  android::sp<android::IGraphicBufferProducer> mProducer;
  RefPtr<DOMVideoCallProvider> mProvider;
  int16_t mType;
  uint16_t mWidth;
  uint16_t mHeight;

  void setProducer(android::sp<android::IGraphicBufferProducer> aProducer);
};

SurfaceControlBack::SurfaceControlBack(RefPtr<DOMVideoCallProvider> aProvider, int16_t aType,
      const SurfaceConfiguration& aConfig)
  : mProducer(nullptr),
    mProvider(aProvider),
    mType(aType),
    mWidth(aConfig.mPreviewSize.mWidth),
    mHeight(aConfig.mPreviewSize.mHeight)
{
}

void
SurfaceControlBack::OnProducerCreated(android::sp<android::IGraphicBufferProducer> aProducer)
{
  LOG("OnProducerCreated: %p", aProducer.get());
  mProducer = aProducer;
  setProducer(mProducer);
}

void
SurfaceControlBack::OnProducerDestroyed()
{
  LOG("OnProducerDestroyed");
  mProducer = nullptr;
  setProducer(nullptr);
}

void
SurfaceControlBack::setProducer(android::sp<android::IGraphicBufferProducer> aProducer)
{
  LOG("setProducer, mType: %d", mType);
  mProvider->SetSurface(mType, aProducer);

  if (aProducer == nullptr) {
    return;
  }
  mProvider->SetSurfaceSize(mType, mWidth, mHeight);
}

void
SurfaceControlBack::Shutdown()
{
}

// WebAPI VideoCallProvider.webidl

NS_IMPL_ADDREF_INHERITED(DOMVideoCallProvider, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(DOMVideoCallProvider, DOMEventTargetHelper)

NS_IMPL_CYCLE_COLLECTION_CLASS(DOMVideoCallProvider)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(DOMVideoCallProvider, DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mProvider)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(DOMVideoCallProvider,
                                                DOMEventTargetHelper)
  tmp->Shutdown();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mProvider)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(DOMVideoCallProvider)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

DOMVideoCallProvider::DOMVideoCallProvider(nsPIDOMWindowInner *aWindow, nsIVideoCallProvider *aProvider)
  : DOMEventTargetHelper(aWindow)
  , mProvider(aProvider)
  , mDisplayControl(nullptr)
  , mPreviewControl(nullptr)
  , mDisplayCallback(nullptr)
  , mPreviewCallback(nullptr)
{
  MOZ_ASSERT(mProvider);
  LOG("constructor");
  mProvider->RegisterCallback(this);
}

DOMVideoCallProvider::~DOMVideoCallProvider()
{
  LOG("deconstructor");
  Shutdown();
}

void
DOMVideoCallProvider::DisconnectFromOwner()
{
  LOG("DisconnectFromOwner");
  DOMEventTargetHelper::DisconnectFromOwner();
  // Event listeners can't be handled anymore, so we can shutdown
  // the DOMVideoCallProvider.
  Shutdown();
}

void
DOMVideoCallProvider::Shutdown()
{
  LOG("Shutdown");

  if (mDisplayCallback) {
    mDisplayCallback->Shutdown();
    delete mDisplayCallback;
    mDisplayCallback = nullptr;
  }

  if (mPreviewCallback) {
    mPreviewCallback->Shutdown();
    delete mPreviewCallback;
    mPreviewCallback = nullptr;
  }

  if (mDisplayControl) {
    mDisplayControl = nullptr;
  }

  if (mPreviewControl) {
    mPreviewControl = nullptr;
  }

  if (mProvider) {
    mProvider->UnregisterCallback(this);
    mProvider = nullptr;
    LOG("null pointer mProvider");
  }
}

JSObject*
DOMVideoCallProvider::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return VideoCallProviderBinding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<Promise>
DOMVideoCallProvider::SetCamera(const Optional<nsAString>& aCamera, ErrorResult& aRv)
{
  LOG("SetCamera: %s", (aCamera.WasPassed() ? NS_ConvertUTF16toUTF8(aCamera.Value()).get() : "null" ));
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  int16_t cameraId = 0;

  if (!aCamera.WasPassed()) {
    cameraId = -1;
  } else if (aCamera.Value().EqualsLiteral("front")) {
      cameraId = 1;
  }

  mProvider->SetCamera(cameraId);

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::GetPreviewStream(const SurfaceConfiguration& aOptions,
    ErrorResult& aRv)
{
  LOG("GetPreviewStream");
  return GetStream(TYPE_PREVIEW, aOptions, aRv);
}

already_AddRefed<Promise>
DOMVideoCallProvider::GetDisplayStream(const SurfaceConfiguration& aOptions,
    ErrorResult& aRv)
{
  LOG("GetDisplayStream");
  return GetStream(TYPE_DISPLAY, aOptions, aRv);
}

already_AddRefed<Promise>
DOMVideoCallProvider::GetStream(const int16_t aType, const SurfaceConfiguration& aOptions, ErrorResult& aRv)
{
  LOG("GetStream, type: %d", aType);
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    aRv.Throw(aRv.StealNSResult());
    return nullptr;
  }

  if (!IsValidSurfaceSize(aOptions.mPreviewSize.mWidth, aOptions.mPreviewSize.mHeight)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<DOMVideoCallProvider> provider = this;
  SurfaceControlBack* callback = new SurfaceControlBack(provider, aType, aOptions);
  RefPtr<nsDOMSurfaceControl> control = new nsDOMSurfaceControl(aOptions, promise, GetOwner(), callback);


  if (aType == TYPE_DISPLAY) {
    mDisplayCallback = callback;
    mDisplayControl = control;
  } else {
    mPreviewCallback = callback;
    mPreviewControl = control;
  }

  return promise.forget();
}

bool
DOMVideoCallProvider::IsValidSurfaceSize(const uint16_t aWidth, const uint16_t aHeight)
{
  // width/height must have values, otherwise UI may display improperly.
  if (aWidth <= 0 || aHeight <= 0) {
    return false;
  } else {
    return true;
  }
}

already_AddRefed<Promise>
DOMVideoCallProvider::SetOrientation(uint16_t aOrientation, ErrorResult& aRv)
{
  LOG("SetOrientation");
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    aRv.Throw(aRv.StealNSResult());
    return nullptr;
  }

  mProvider->SetDeviceOrientation(aOrientation);

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::SetZoom(float aZoom, ErrorResult& aRv)
{
  LOG("SetZoom: %f", aZoom);
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    aRv.Throw(aRv.StealNSResult());
    return nullptr;
  }

  mProvider->SetZoom(aZoom);

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::SendSessionModifyRequest(DOMVideoCallProfile& aFrom,
                                               DOMVideoCallProfile& aTo,
                                               ErrorResult& aRv)
{
  LOG("SendSessionModifyRequest, from {%d, %d} to {%d, %d}", (int32_t)aFrom.Quality(), (int32_t)aFrom.State(), (int32_t)aTo.Quality(), (int32_t)aTo.State());
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    aRv.Throw(aRv.StealNSResult());
    return nullptr;
  }

  mProvider->SendSessionModifyRequest(&aFrom, &aTo);

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::SendSessionModifyResponse(DOMVideoCallProfile& aResponse,
                                                ErrorResult& aRv)
{
  LOG("SendSessionModifyResponse response {%d, %d}", (int32_t)aResponse.Quality(), (int32_t)aResponse.State());
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    aRv.Throw(aRv.StealNSResult());
    return nullptr;
  }

  mProvider->SendSessionModifyResponse(&aResponse);

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::RequestCameraCapabilities(ErrorResult& aRv)
{
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetOwner());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    aRv.Throw(aRv.StealNSResult());
    return nullptr;
  }

  mProvider->RequestCameraCapabilities();

  return promise.forget();
}

// Class API

nsresult
DOMVideoCallProvider::DispatchSessionModifyRequestEvent(const nsAString& aType,
                                                        DOMVideoCallProfile* aRequest)
{
  VideoCallSessionModifyRequestEventInit init;
  init.mRequest = aRequest;
  RefPtr<VideoCallSessionModifyRequestEvent> event =
      VideoCallSessionModifyRequestEvent::Constructor(this, aType, init);
  return DispatchTrustedEvent(event);
}

nsresult
DOMVideoCallProvider::DispatchSessionModifyResponseEvent(const nsAString& aType,
                                                         const uint16_t aStatus,
                                                         DOMVideoCallProfile* aRequest,
                                                         DOMVideoCallProfile* aResponse)
{
  VideoCallSessionModifyResponseEventInit init;
  init.mStatus = static_cast<VideoCallSessionStatus>(aStatus);
  init.mRequest = aRequest;
  init.mResponse = aResponse;
  RefPtr<VideoCallSessionModifyResponseEvent> event =
      VideoCallSessionModifyResponseEvent::Constructor(this, aType, init);
  return DispatchTrustedEvent(event);
}

nsresult
DOMVideoCallProvider::DispatCallSessionEvent(const nsAString& aType, const int16_t aEvent)
{
  VideoCallSessionChangeEventInit init;
  init.mType = static_cast<VideoCallSessionChangeType>(aEvent);
  RefPtr<VideoCallSessionChangeEvent> event =
      VideoCallSessionChangeEvent::Constructor(this, aType, init);
  return DispatchTrustedEvent(event);
}

nsresult
DOMVideoCallProvider::DispatchChangePeerDimensionsEvent(const nsAString& aType,
                                                        const uint16_t aWidth,
                                                        const uint16_t aHeight)
{
  VideoCallPeerDimensionsEventInit init;
  init.mWidth = aWidth;
  init.mHeight = aHeight;
  RefPtr<VideoCallPeerDimensionsEvent> event =
      VideoCallPeerDimensionsEvent::Constructor(this, aType, init);
  return DispatchTrustedEvent(event);
}

nsresult
DOMVideoCallProvider::DispatchCameraCapabilitiesEvent(const nsAString& aType)
{
  VideoCallCameraCapabilitiesChangeEventInit init;
  // TODO to complete the body.
  RefPtr<VideoCallCameraCapabilitiesChangeEvent> event =
      VideoCallCameraCapabilitiesChangeEvent::Constructor(this, aType, init);
  return NS_OK;
}

nsresult
DOMVideoCallProvider::DispatchVideoQualityChangeEvent(const nsAString& aType, const VideoCallQuality aQuality)
{
  VideoCallQualityEventInit init;
  init.mQuality = static_cast<VideoCallQuality>(aQuality);
  RefPtr<VideoCallQualityEvent> event =
      VideoCallQualityEvent::Constructor(this, aType, init);
  return DispatchTrustedEvent(event);
}

void
DOMVideoCallProvider::SetSurface(const int16_t aType, android::sp<android::IGraphicBufferProducer>& aProducer)
{
  LOG("SetSurface, type: %d, surface: %p", aType, aProducer.get());
  if (aType == TYPE_DISPLAY) {
    mProvider->SetDisplaySurface(aProducer);
  } else {
    mProvider->SetPreviewSurface(aProducer);
  }
}

void
DOMVideoCallProvider::SetSurfaceSize(const int16_t aType, const uint16_t aWidth, const uint16_t aHeight)
{

  RefPtr<nsDOMSurfaceControl> control;
  if (aType == TYPE_DISPLAY) {
    control = mDisplayControl;
  } else {
    control = mPreviewControl;
  }

  if (!control) {
    return;
  }
}

// nsIVideoCallCallback

NS_IMETHODIMP
DOMVideoCallProvider::OnReceiveSessionModifyRequest(nsIVideoCallProfile *request)
{
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnReceiveSessionModifyResponse(uint16_t status,
    nsIVideoCallProfile *request, nsIVideoCallProfile *response)
{
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnHandleCallSessionEvent(int16_t event)
{
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnChangePeerDimensions(uint16_t width, uint16_t height)
{
  return NS_OK;
}
NS_IMETHODIMP
DOMVideoCallProvider::OnChangeCameraCapabilities(nsIVideoCallCameraCapabilities *capabilities)
{
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnChangeVideoQuality(uint16_t quality)
{
  return NS_OK;
}

} // namespace dom
} // namespace mozilla