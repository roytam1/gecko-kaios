/* (c) 2017 KAI OS TECHNOLOGIES (HONG KONG) LIMITED All rights reserved. This
 * file or any portion thereof may not be reproduced or used in any manner
 * whatsoever without the express written permission of KAI OS TECHNOLOGIES
 * (HONG KONG) LIMITED. KaiOS is the trademark of KAI OS TECHNOLOGIES (HONG KONG)
 * LIMITED or its affiliate company and may be registered in some jurisdictions.
 * All other trademarks are the property of their respective owners.
 */

#include "DOMSurfaceControl.h"
#include "IDOMSurfaceControlCallback.h"
#include "mozilla/dom/DOMVideoCallProvider.h"
#include "mozilla/dom/DOMVideoCallProfile.h"
#include "mozilla/dom/DOMVideoCallCameraCapabilities.h"
#include "nsPIDOMWindow.h"
#include "mozilla/dom/VideoCallCameraCapabilitiesChangeEvent.h"
#include "mozilla/dom/VideoCallPeerDimensionsEvent.h"
#include "mozilla/dom/VideoCallQualityEvent.h"
#include "mozilla/dom/VideoCallSessionChangeEvent.h"
#include "mozilla/dom/VideoCallSessionModifyRequestEvent.h"
#include "mozilla/dom/VideoCallSessionModifyResponseEvent.h"
#include "nsIVideoCallProvider.h"

#ifdef FEED_TEST_DATA_TO_PRODUCER
#include <cutils/properties.h>
#include "TestDataSourceCamera.h"
#endif

#include <android/log.h>
#undef LOG
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "DOMVideoCallProvider" , ## args)

using mozilla::ErrorResult;

namespace mozilla {
namespace dom {

const int16_t TYPE_DISPLAY = 0;
const int16_t TYPE_PREVIEW = 1;

const int16_t ROTATE_ANGLE = 90;

#ifdef FEED_TEST_DATA_TO_PRODUCER
class TestDataSourceResolutionResultListener : public ITestDataSourceResolutionResultListener
{
public:
  TestDataSourceResolutionResultListener()
    : ITestDataSourceResolutionResultListener()
  {
  }

  virtual void SetPreviewSurfaceControl(nsDOMSurfaceControl* aPreviewSurfaceControl)
  {
    mPreviewSurfaceControl = aPreviewSurfaceControl;
  }

  virtual void SetDisplaySurfaceControl(nsDOMSurfaceControl* aDisplaySurfaceControl)
  {
    mDisplaySurfaceControl = aDisplaySurfaceControl;
  }

  virtual void onChangeCameraCapabilities(unsigned int aResultWidth,
                                          unsigned int aResultHeight)
  {
    if (mPreviewSurfaceControl) {
      mPreviewSurfaceControl->SetDataSourceSize(aResultWidth, aResultHeight);
    }
  }

  virtual void onChangePeerDimensions(unsigned int aResultWidth,
                                      unsigned int aResultHeight)
  {
     if (mDisplaySurfaceControl) {
      mDisplaySurfaceControl->SetDataSourceSize(aResultWidth, aResultHeight);
    }   
  }

protected:
  virtual ~TestDataSourceResolutionResultListener() { }

private:
  nsDOMSurfaceControl* mPreviewSurfaceControl;
  nsDOMSurfaceControl* mDisplaySurfaceControl;
};
#endif

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
  mProvider->SetSurface(mType, aProducer, mWidth, mHeight);

  if (aProducer == nullptr) {
    return;
  }
  mProvider->SetDataSourceSize(mType, mWidth, mHeight);
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
  , mOrientation(0)
  , mZoom(0)
  , mMaxZoom(0.0)
  , mZoomSupported(false)
  , mProvider(aProvider)
  , mDisplayControl(nullptr)
  , mPreviewControl(nullptr)
  , mDisplayCallback(nullptr)
  , mPreviewCallback(nullptr)
#ifdef FEED_TEST_DATA_TO_PRODUCER
  , mResolutionResultListener(NULL)
  , mTestDataSource(NULL)
  , mIsLoopback(false)
#endif
{
  MOZ_ASSERT(mProvider);
  LOG("constructor");
  mProvider->RegisterCallback(this);
#ifdef FEED_TEST_DATA_TO_PRODUCER
  char prop[128];
  if (property_get("vt.surface.test", prop, NULL) != 0) {
    if (strcmp(prop, "1") == 0) {
      mIsLoopback = true;
    }
  }
#endif
}

DOMVideoCallProvider::~DOMVideoCallProvider()
{
  LOG("deconstructor");
  Shutdown();

#ifdef FEED_TEST_DATA_TO_PRODUCER
  if (mTestDataSource) {
    delete mTestDataSource;
  }

  if (mResolutionResultListener) {
    delete mResolutionResultListener;
  }
#endif
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

#ifdef FEED_TEST_DATA_TO_PRODUCER 
  if (mTestDataSource) {
    mTestDataSource->Stop();
  }
#endif
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
  } else {
    if (aCamera.Value().EqualsLiteral("front")) {
      cameraId = 1;
    }

    mCamera = aCamera.Value();
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

#ifdef FEED_TEST_DATA_TO_PRODUCER
    if(mIsLoopback) {
      if (mResolutionResultListener == NULL) {
        mResolutionResultListener = new TestDataSourceResolutionResultListener();
      }

      if (mTestDataSource == NULL) {
        mTestDataSource = new TestDataSourceCamera(mResolutionResultListener);
      }
      mResolutionResultListener->SetDisplaySurfaceControl(mDisplayControl);
    }
#endif
  } else {
    mPreviewCallback = callback;
    mPreviewControl = control;

#ifdef FEED_TEST_DATA_TO_PRODUCER
    if(mIsLoopback) {
      if (mResolutionResultListener == NULL) {
        mResolutionResultListener = new TestDataSourceResolutionResultListener();
      }

      if (mTestDataSource == NULL) {
        mTestDataSource = new TestDataSourceCamera(mResolutionResultListener);
      }
      mResolutionResultListener->SetPreviewSurfaceControl(mPreviewControl);
    }
#endif
  }

  return promise.forget();
}

bool
DOMVideoCallProvider::ValidOrientation(uint16_t aOrientation)
{
  return (aOrientation % ROTATE_ANGLE) == 0;
}

bool
DOMVideoCallProvider::ValidZoom(uint16_t aZoom)
{
  if (!mZoomSupported) {
    return false;
  }

  if (aZoom > mMaxZoom) {
    return false;
  }

  return true;
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

  if (!ValidOrientation(aOrientation)) {
    LOG("Invalid orientation %d", aOrientation);
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

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
  mOrientation = aOrientation;

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::SetZoom(float aZoom, ErrorResult& aRv)
{
  LOG("SetZoom: %f", aZoom);

  if (!ValidZoom(aZoom)) {
    LOG("Invalid zoom: %f", aZoom);
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

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
  mZoom = aZoom;

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::SendSessionModifyRequest(const VideoCallProfile& aFrom,
                                               const VideoCallProfile& aTo,
                                               ErrorResult& aRv)
{
  LOG("SendSessionModifyRequest, from {quality: %d, state: %d} to {quality: %d, state: %d}",
      static_cast<int16_t>(aFrom.mQuality.Value()),
      static_cast<int16_t>(aFrom.mState.Value()),
      static_cast<int16_t>(aTo.mQuality.Value()),
      static_cast<int16_t>(aTo.mState.Value()));
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

  RefPtr<DOMVideoCallProfile> from =
    new DOMVideoCallProfile(aFrom.mQuality.Value(),
                            aFrom.mState.Value());
  RefPtr<DOMVideoCallProfile> to =
    new DOMVideoCallProfile(aTo.mQuality.Value(),
                            aTo.mState.Value());

  mProvider->SendSessionModifyRequest(from, to);

  return promise.forget();
}

already_AddRefed<Promise>
DOMVideoCallProvider::SendSessionModifyResponse(const VideoCallProfile& aResponse,
                                                ErrorResult& aRv)
{
  LOG("SendSessionModifyResponse response {quality: %d, state: %d}",
      static_cast<int16_t>(aResponse.mQuality.Value()),
      static_cast<int16_t>(aResponse.mState.Value()));
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

  RefPtr<DOMVideoCallProfile> response =
    new DOMVideoCallProfile(aResponse.mQuality.Value(),
                            aResponse.mState.Value());

  mProvider->SendSessionModifyResponse(response);

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
                                                        nsIVideoCallProfile *aRequest)
{
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(GetOwner()))) {
    return NS_ERROR_FAILURE;
  }

  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> jsRequest(cx);
  nsresult rv = ConvertToJsValue(aRequest, &jsRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  VideoCallSessionModifyRequestEventInit init;
  init.mRequest = jsRequest;
  RefPtr<VideoCallSessionModifyRequestEvent> event =
    VideoCallSessionModifyRequestEvent::Constructor(this, aType, init);

  uint16_t quality;
  uint16_t state;
  aRequest->GetQuality(&quality);
  aRequest->GetState(&state);

  LOG("DispatchSessionModifyRequestEvent request {quality: %d, state: %d}", quality, state);

  return DispatchTrustedEvent(event);
}

nsresult
DOMVideoCallProvider::DispatchSessionModifyResponseEvent(const nsAString& aType,
                                                         const uint16_t aStatus,
                                                         DOMVideoCallProfile* aRequest,
                                                         DOMVideoCallProfile* aResponse)
{
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(GetOwner()))) {
    return NS_ERROR_FAILURE;
  }

  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> jsRequest(cx);
  nsresult rv = ConvertToJsValue(aRequest, &jsRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  JS::Rooted<JS::Value> jsResponse(cx);
  rv = ConvertToJsValue(aResponse, &jsResponse);
  NS_ENSURE_SUCCESS(rv, rv);

  VideoCallSessionModifyResponseEventInit init;
  init.mStatus = static_cast<VideoCallSessionStatus>(aStatus);
  init.mRequest = jsRequest;
  init.mResponse = jsResponse;
  RefPtr<VideoCallSessionModifyResponseEvent> event =
      VideoCallSessionModifyResponseEvent::Constructor(this, aType, init);

  uint16_t requestQuality;
  uint16_t requestState;
  aRequest->GetQuality(&requestQuality);
  aRequest->GetState(&requestState);

  uint16_t responseQuality;
  uint16_t responseState;
  aResponse->GetQuality(&responseQuality);
  aResponse->GetState(&responseState);

  LOG("DispatchSessionModifyResponseEvent, status: %d, request {quality: %d, state: %d}, response {quality: %d, state: %d}",
    aStatus, requestQuality, requestState, responseQuality, responseState);
  return DispatchTrustedEvent(event);
}

nsresult
DOMVideoCallProvider::ConvertToJsValue(nsIVideoCallProfile *aProfile,
                                       JS::Rooted<JS::Value>* jsResult)
{
  uint16_t quality;
  uint16_t state;
  aProfile->GetQuality(&quality);
  aProfile->GetState(&state);

  VideoCallProfile requestParams;
  VideoCallQuality& resultQuality = requestParams.mQuality.Construct();
  VideoCallState& resultState = requestParams.mState.Construct();
  resultQuality = static_cast<VideoCallQuality>(quality);
  resultState = static_cast<VideoCallState>(state);

  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(GetOwner()))) {
    return NS_ERROR_FAILURE;
  }

  JSContext* cx = jsapi.cx();
  if (!ToJSValue(cx, requestParams, jsResult)) {
    JS_ClearPendingException(cx);
    return NS_ERROR_TYPE_ERR;
  }

  return NS_OK;
}

nsresult
DOMVideoCallProvider::DispatCallSessionEvent(const nsAString& aType, const int16_t aEvent)
{
  LOG("DispatCallSessionEvent aEvent: %d", aEvent);
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
DOMVideoCallProvider::DispatchCameraCapabilitiesEvent(const nsAString& aType, nsIVideoCallCameraCapabilities* capabilities)
{
  VideoCallCameraCapabilitiesChangeEventInit init;
  capabilities->GetWidth(&init.mWidth);
  capabilities->GetHeight(&init.mHeight);
  capabilities->GetMaxZoom(&init.mMaxZoom);
  capabilities->GetZoomSupported(&init.mZoomSupported);

  capabilities->GetMaxZoom(&mMaxZoom);
  capabilities->GetZoomSupported(&mZoomSupported);

  RefPtr<VideoCallCameraCapabilitiesChangeEvent> event =
      VideoCallCameraCapabilitiesChangeEvent::Constructor(this, aType, init);
  return DispatchTrustedEvent(event);
}

nsresult
DOMVideoCallProvider::DispatchVideoQualityChangeEvent(const nsAString& aType, const uint16_t aQuality)
{
  VideoCallQualityEventInit init;
  init.mQuality = static_cast<VideoCallQuality>(aQuality);
  RefPtr<VideoCallQualityEvent> event =
      VideoCallQualityEvent::Constructor(this, aType, init);
  return DispatchTrustedEvent(event);
}

void
DOMVideoCallProvider::SetSurface(const int16_t aType, android::sp<android::IGraphicBufferProducer>& aProducer,
                                 const uint16_t aWidth, const uint16_t aHeight)
{
  LOG("SetDisplaySurface, type: %d, producer: %p", aType, aProducer.get());
  if (aType == TYPE_DISPLAY) {
    mProvider->SetDisplaySurface(aProducer, aWidth, aHeight);
#ifdef FEED_TEST_DATA_TO_PRODUCER 
    if (mIsLoopback && mTestDataSource) {
      mTestDataSource->SetDisplaySurface(aProducer, aWidth, aHeight);
    }
#endif
  } else {
    mProvider->SetPreviewSurface(aProducer, aWidth, aHeight);
#ifdef FEED_TEST_DATA_TO_PRODUCER 
    if (mIsLoopback && mTestDataSource) {
      mTestDataSource->SetPreviewSurface(aProducer, aWidth, aHeight);
    }
#endif
  }
}

void
DOMVideoCallProvider::SetDataSourceSize(const int16_t aType, const uint16_t aWidth, const uint16_t aHeight)
{
  if (aType == TYPE_DISPLAY) {
    mDisplayControl->SetDataSourceSize(aWidth, aHeight);
  } else {
    mPreviewControl->SetDataSourceSize(aWidth, aHeight);
  }
}

// nsIVideoCallCallback

NS_IMETHODIMP
DOMVideoCallProvider::OnReceiveSessionModifyRequest(nsIVideoCallProfile *request)
{
  DispatchSessionModifyRequestEvent(NS_LITERAL_STRING("sessionmodifyrequest"), request);
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnReceiveSessionModifyResponse(uint16_t status,
    nsIVideoCallProfile *request, nsIVideoCallProfile *response)
{
  LOG("OnReceiveSessionModifyResponse, status: %d", status);
  DispatchSessionModifyResponseEvent(NS_LITERAL_STRING("sessionmodifyresponse"), status,
      static_cast<DOMVideoCallProfile*>(request),
      static_cast<DOMVideoCallProfile*>(response));
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnHandleCallSessionEvent(int16_t event)
{
  DispatCallSessionEvent(NS_LITERAL_STRING("callsessionevent"), event);
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnChangePeerDimensions(uint16_t aWidth, uint16_t aHeight)
{
  DispatchChangePeerDimensionsEvent(NS_LITERAL_STRING("changepeerdimensions"), aWidth, aHeight);
  SetDataSourceSize(TYPE_DISPLAY, aWidth, aHeight);
  return NS_OK;
}
NS_IMETHODIMP
DOMVideoCallProvider::OnChangeCameraCapabilities(nsIVideoCallCameraCapabilities *capabilities)
{
  LOG("OnChangeCameraCapabilities");
  DispatchCameraCapabilitiesEvent(NS_LITERAL_STRING("changecameracapabilities"), capabilities);
  if (mPreviewControl) {
    uint16_t width;
    uint16_t height;
    capabilities->GetWidth(&width);
    capabilities->GetHeight(&height);
    SetDataSourceSize(TYPE_PREVIEW, width, height);
  }
  return NS_OK;
}

NS_IMETHODIMP
DOMVideoCallProvider::OnChangeVideoQuality(uint16_t quality)
{
  DispatchVideoQualityChangeEvent(NS_LITERAL_STRING("changevideoquality"), quality);
  return NS_OK;
}

} // namespace dom
} // namespace mozilla