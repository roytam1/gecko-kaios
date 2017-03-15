/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMCameraManager.h"
#include "nsDebug.h"
#include "jsapi.h"
#include "Navigator.h"
#include "nsPIDOMWindow.h"
#include "mozilla/Services.h"
#include "nsContentPermissionHelper.h"
#include "nsIContentPermissionPrompt.h"
#include "nsIObserverService.h"
#include "nsIPermissionManager.h"
#include "nsIScriptObjectPrincipal.h"
#include "DOMCameraControl.h"
#include "DOMSurfaceControl.h"
#include "IDOMSurfaceControlCallback.h"
#include "nsDOMClassInfo.h"
#include "CameraCommon.h"
#include "CameraPreferences.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "nsQueryObject.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace android;

#ifdef FEED_TEST_DATA_TO_PRODUCER
#include <cutils/properties.h>
#endif

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(nsDOMCameraManager, mWindow)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMCameraManager)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMCameraManager)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMCameraManager)

/**
 * Global camera logging object
 *
 * Set the NSPR_LOG_MODULES environment variable to enable logging
 * in a debug build, e.g. NSPR_LOG_MODULES=Camera:5
 */
LogModule*
GetCameraLog()
{
  static LazyLogModule sLog("Camera");
  return sLog;
}

class DOMSurfaceControlCallback : public IDOMSurfaceControlCallback
{
public:
  DOMSurfaceControlCallback(nsDOMCameraManager* aCameraManager) :
    mDOMCameraManager(aCameraManager) {
  }

  virtual void OnProducerCreated(
    android::sp<android::IGraphicBufferProducer> aProducer) override;
  virtual void OnProducerDestroyed() override;

  virtual ~DOMSurfaceControlCallback() { }

private:
  nsDOMCameraManager* mDOMCameraManager;
};

void DOMSurfaceControlCallback::OnProducerCreated(android::sp<android::IGraphicBufferProducer> aProducer)
{
#ifdef FEED_TEST_DATA_TO_PRODUCER
  char prop[128];
  if (property_get("vt.surface.test", prop, NULL) != 0) {
    if (strcmp(prop, "2") == 0) {
      if(mDOMCameraManager) {
        mDOMCameraManager->mProducer = aProducer;
        mDOMCameraManager->TestSurfaceInput();
      }
    }
  }
#endif
}

void DOMSurfaceControlCallback::OnProducerDestroyed()
{

}

::WindowTable* nsDOMCameraManager::sActiveWindows = nullptr;
::WindowTable* nsDOMCameraManager::sSurfaceActiveWindows = nullptr;

nsDOMCameraManager::nsDOMCameraManager(nsPIDOMWindowInner* aWindow)
  :
#ifdef FEED_TEST_DATA_TO_PRODUCER
    mTestImage(NULL)
  , mTestImage2(NULL)
  , mTestImageIndex(0)
  , mIsTestRunning(false)
  ,
#endif
    mWindowId(aWindow->WindowID())
  , mPermission(nsIPermissionManager::DENY_ACTION)
  , mWindow(aWindow)
{
  /* member initializers and constructor code */
  DOM_CAMERA_LOGT("%s:%d : this=%p, windowId=%" PRIx64 "\n", __func__, __LINE__, this, mWindowId);
  MOZ_COUNT_CTOR(nsDOMCameraManager);
}

nsDOMCameraManager::~nsDOMCameraManager()
{
  if (mDOMSurfaceControlCallback) {
    delete mDOMSurfaceControlCallback;
  }
#ifdef FEED_TEST_DATA_TO_PRODUCER
  if (mTestImage) {
    delete [] mTestImage;
  }

  if (mTestImage2) {
    delete [] mTestImage2;
  }
#endif
  /* destructor code */
  MOZ_COUNT_DTOR(nsDOMCameraManager);
  DOM_CAMERA_LOGT("%s:%d : this=%p\n", __func__, __LINE__, this);
}

/* static */
void
nsDOMCameraManager::GetListOfCameras(nsTArray<nsString>& aList, ErrorResult& aRv)
{
  aRv = ICameraControl::GetListOfCameras(aList);
}

/* static */
bool
nsDOMCameraManager::HasSupport(JSContext* aCx, JSObject* aGlobal)
{
  return Navigator::HasCameraSupport(aCx, aGlobal);
}

/* static */
bool
nsDOMCameraManager::CheckPermission(nsPIDOMWindowInner* aWindow)
{
  nsCOMPtr<nsIPermissionManager> permMgr =
    services::GetPermissionManager();
  NS_ENSURE_TRUE(permMgr, false);

  uint32_t permission = nsIPermissionManager::DENY_ACTION;
  permMgr->TestPermissionFromWindow(aWindow, "camera", &permission);
  if (permission != nsIPermissionManager::ALLOW_ACTION &&
      permission != nsIPermissionManager::PROMPT_ACTION) {
    return false;
  }

  return true;
}

/* static */
already_AddRefed<nsDOMCameraManager>
nsDOMCameraManager::CreateInstance(nsPIDOMWindowInner* aWindow)
{
  // Initialize the shared active window tracker
  if (!sActiveWindows) {
    sActiveWindows = new ::WindowTable();
  }

  if (!sSurfaceActiveWindows) {
    sSurfaceActiveWindows = new ::SurfaceWindowTable();
  }
  RefPtr<nsDOMCameraManager> cameraManager =
    new nsDOMCameraManager(aWindow);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    DOM_CAMERA_LOGE("Camera manager failed to get observer service\n");
    return nullptr;
  }

  nsresult rv = obs->AddObserver(cameraManager, "xpcom-shutdown", true);
  if (NS_FAILED(rv)) {
    DOM_CAMERA_LOGE("Camera manager failed to add 'xpcom-shutdown' observer (0x%x)\n", rv);
    return nullptr;
  }

  return cameraManager.forget();
}

class CameraPermissionRequest : public nsIContentPermissionRequest
                              , public nsIRunnable
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSICONTENTPERMISSIONREQUEST
  NS_DECL_NSIRUNNABLE
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(CameraPermissionRequest,
                                           nsIContentPermissionRequest)

  CameraPermissionRequest(nsIPrincipal* aPrincipal,
                          nsPIDOMWindowInner* aWindow,
                          RefPtr<nsDOMCameraManager> aManager,
                          uint32_t aCameraId,
                          const CameraConfiguration& aInitialConfig,
                          RefPtr<Promise> aPromise)
    : mPrincipal(aPrincipal)
    , mWindow(aWindow)
    , mCameraManager(aManager)
    , mCameraId(aCameraId)
    , mInitialConfig(aInitialConfig)
    , mPromise(aPromise)
    , mRequester(new nsContentPermissionRequester(mWindow))
  { }

protected:
  virtual ~CameraPermissionRequest() { }

  nsresult DispatchCallback(uint32_t aPermission);
  void CallAllow();
  void CallCancel();
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  RefPtr<nsDOMCameraManager> mCameraManager;
  uint32_t mCameraId;
  CameraConfiguration mInitialConfig;
  RefPtr<Promise> mPromise;
  nsCOMPtr<nsIContentPermissionRequester> mRequester;
};

NS_IMPL_CYCLE_COLLECTION(CameraPermissionRequest, mWindow, mPromise)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CameraPermissionRequest)
  NS_INTERFACE_MAP_ENTRY(nsIContentPermissionRequest)
  NS_INTERFACE_MAP_ENTRY(nsIRunnable)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIContentPermissionRequest)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(CameraPermissionRequest)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CameraPermissionRequest)

NS_IMETHODIMP
CameraPermissionRequest::Run()
{
  return nsContentPermissionUtils::AskPermission(this, mWindow);
}

NS_IMETHODIMP
CameraPermissionRequest::GetPrincipal(nsIPrincipal** aRequestingPrincipal)
{
  NS_ADDREF(*aRequestingPrincipal = mPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
CameraPermissionRequest::GetWindow(mozIDOMWindow** aRequestingWindow)
{
  NS_ADDREF(*aRequestingWindow = mWindow);
  return NS_OK;
}

NS_IMETHODIMP
CameraPermissionRequest::GetElement(nsIDOMElement** aElement)
{
  *aElement = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
CameraPermissionRequest::Cancel()
{
  return DispatchCallback(nsIPermissionManager::DENY_ACTION);
}

NS_IMETHODIMP
CameraPermissionRequest::Allow(JS::HandleValue aChoices)
{
  MOZ_ASSERT(aChoices.isUndefined());
  return DispatchCallback(nsIPermissionManager::ALLOW_ACTION);
}

NS_IMETHODIMP
CameraPermissionRequest::GetRequester(nsIContentPermissionRequester** aRequester)
{
  NS_ENSURE_ARG_POINTER(aRequester);

  nsCOMPtr<nsIContentPermissionRequester> requester = mRequester;
  requester.forget(aRequester);
  return NS_OK;
}

nsresult
CameraPermissionRequest::DispatchCallback(uint32_t aPermission)
{
  nsCOMPtr<nsIRunnable> callbackRunnable;
  if (aPermission == nsIPermissionManager::ALLOW_ACTION) {
    callbackRunnable = NS_NewRunnableMethod(this, &CameraPermissionRequest::CallAllow);
  } else {
    callbackRunnable = NS_NewRunnableMethod(this, &CameraPermissionRequest::CallCancel);
  }
  return NS_DispatchToMainThread(callbackRunnable);
}

void
CameraPermissionRequest::CallAllow()
{
  mCameraManager->PermissionAllowed(mCameraId, mInitialConfig, mPromise);
}

void
CameraPermissionRequest::CallCancel()
{
  mCameraManager->PermissionCancelled(mCameraId, mInitialConfig, mPromise);
}

NS_IMETHODIMP
CameraPermissionRequest::GetTypes(nsIArray** aTypes)
{
  nsTArray<nsString> emptyOptions;
  return nsContentPermissionUtils::CreatePermissionArray(NS_LITERAL_CSTRING("camera"),
                                                         NS_LITERAL_CSTRING("unused"),
                                                         emptyOptions,
                                                         aTypes);
}

#ifdef MOZ_WIDGET_GONK
/* static */ void
nsDOMCameraManager::PreinitCameraHardware()
{
  nsDOMCameraControl::PreinitCameraHardware();
}
#endif

already_AddRefed<Promise>
nsDOMCameraManager::GetCamera(const nsAString& aCamera,
                              const CameraConfiguration& aInitialConfig,
                              ErrorResult& aRv)
{
  DOM_CAMERA_LOGT("%s:%d\n", __func__, __LINE__);

  uint32_t cameraId = 0;  // back (or forward-facing) camera by default
  if (aCamera.EqualsLiteral("front")) {
    cameraId = 1;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  if (mPermission == nsIPermissionManager::ALLOW_ACTION) {
    PermissionAllowed(cameraId, aInitialConfig, promise);
    return promise.forget();
  }

  nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(mWindow);
  if (!sop) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsCOMPtr<nsIPrincipal> principal = sop->GetPrincipal();
  // If we are a CERTIFIED app, we can short-circuit the permission check,
  // which gets us a performance win.
  uint16_t status = nsIPrincipal::APP_STATUS_NOT_INSTALLED;
  principal->GetAppStatus(&status);
  // Unprivileged mochitests always fail the dispatched permission check,
  // even if permission to the camera has been granted.
  bool immediateCheck = false;
  CameraPreferences::GetPref("camera.control.test.permission", immediateCheck);
  if ((status == nsIPrincipal::APP_STATUS_CERTIFIED || immediateCheck) && CheckPermission(mWindow)) {
    PermissionAllowed(cameraId, aInitialConfig, promise);
    return promise.forget();
  }

  nsCOMPtr<nsIRunnable> permissionRequest =
    new CameraPermissionRequest(principal, mWindow, this, cameraId,
                                aInitialConfig, promise);

  NS_DispatchToMainThread(permissionRequest);
  return promise.forget();
}

already_AddRefed<Promise>
nsDOMCameraManager::GetSurface(const SurfaceConfiguration& aInitialConfig, mozilla::ErrorResult& aRv)
{
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(mWindow);
  if (!sop) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // Creating this object will trigger the aOnSuccess callback
  //  (or the aOnError one, if it fails).
  mDOMSurfaceControlCallback = new DOMSurfaceControlCallback(this);
  RefPtr<nsDOMSurfaceControl> surfaceControl =
    new nsDOMSurfaceControl(aInitialConfig, promise, mWindow, mDOMSurfaceControlCallback);

  RegisterSurface(surfaceControl);

  return promise.forget();
}

void
nsDOMCameraManager::PermissionAllowed(uint32_t aCameraId,
                                      const CameraConfiguration& aInitialConfig,
                                      Promise* aPromise)
{
  mPermission = nsIPermissionManager::ALLOW_ACTION;

  // Creating this object will trigger the aOnSuccess callback
  //  (or the aOnError one, if it fails).
  RefPtr<nsDOMCameraControl> cameraControl =
    new nsDOMCameraControl(aCameraId, aInitialConfig, aPromise, mWindow);

  Register(cameraControl);
}

void
nsDOMCameraManager::PermissionCancelled(uint32_t aCameraId,
                                        const CameraConfiguration& aInitialConfig,
                                        Promise* aPromise)
{
  mPermission = nsIPermissionManager::DENY_ACTION;
  aPromise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
}

void
nsDOMCameraManager::Register(nsDOMCameraControl* aDOMCameraControl)
{
  DOM_CAMERA_LOGI(">>> Register( aDOMCameraControl = %p ) mWindowId = 0x%" PRIx64 "\n", aDOMCameraControl, mWindowId);
  MOZ_ASSERT(NS_IsMainThread());

  CameraControls* controls = sActiveWindows->Get(mWindowId);
  if (!controls) {
    controls = new CameraControls();
    sActiveWindows->Put(mWindowId, controls);
  }

  // Remove any stale CameraControl objects to limit our memory usage
  uint32_t i = controls->Length();
  while (i > 0) {
    --i;
    RefPtr<nsDOMCameraControl> cameraControl =
      do_QueryReferent(controls->ElementAt(i));
    if (!cameraControl) {
      controls->RemoveElementAt(i);
    }
  }

  // Put the camera control into the hash table
  nsWeakPtr cameraControl =
    do_GetWeakReference(static_cast<DOMMediaStream*>(aDOMCameraControl));
  controls->AppendElement(cameraControl);
}

void
nsDOMCameraManager::Shutdown(uint64_t aWindowId)
{
  DOM_CAMERA_LOGI(">>> Shutdown( aWindowId = 0x%" PRIx64 " )\n", aWindowId);
  MOZ_ASSERT(NS_IsMainThread());

  CameraControls* controls = sActiveWindows->Get(aWindowId);
  if (controls) {
    uint32_t i = controls->Length();
    while (i > 0) {
      --i;
      RefPtr<nsDOMCameraControl> cameraControl =
        do_QueryReferent(controls->ElementAt(i));
      if (cameraControl) {
        cameraControl->Shutdown();
      }
    }
    controls->Clear();
    sActiveWindows->Remove(aWindowId);
  }

  //==Surface test start==
  //Stop test if any
  mIsTestRunning = false;
  SurfaceControls* surfaceControls = sSurfaceActiveWindows->Get(aWindowId);
  if (surfaceControls) {
    uint32_t i = surfaceControls->Length();
    while (i > 0) {
      --i;
      RefPtr<nsDOMSurfaceControl> surfaceControl =
        do_QueryReferent(surfaceControls->ElementAt(i));
      if (surfaceControl) {
        surfaceControl->Shutdown();
      }
    }
    surfaceControls->Clear();
    sSurfaceActiveWindows->Remove(aWindowId);
  }

  //==Surface test end==
}

void
nsDOMCameraManager::RegisterSurface(nsDOMSurfaceControl* aDOMSurfaceControl)
{
  DOM_CAMERA_LOGI(">>> Register( nsDOMSurfaceControl = %p ) mWindowId = 0x%" PRIx64 "\n", aDOMSurfaceControl, mWindowId);
  MOZ_ASSERT(NS_IsMainThread());

  SurfaceControls* controls = sSurfaceActiveWindows->Get(mWindowId);
  if (!controls) {
    controls = new SurfaceControls();
    sSurfaceActiveWindows->Put(mWindowId, controls);
  }

  // Remove any stale SurfaceControl objects to limit our memory usage
  uint32_t i = controls->Length();
  while (i > 0) {
    --i;
    RefPtr<nsDOMSurfaceControl> surfaceControl =
      do_QueryReferent(controls->ElementAt(i));
    if (!surfaceControl) {
      controls->RemoveElementAt(i);
    }
  }

  // Put the surface control into the hash table
  nsWeakPtr surfaceControl =
    do_GetWeakReference(static_cast<DOMMediaStream*>(aDOMSurfaceControl));
  controls->AppendElement(surfaceControl);
}

void
nsDOMCameraManager::XpComShutdown()
{
  DOM_CAMERA_LOGI(">>> XPCOM Shutdown\n");
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  obs->RemoveObserver(this, "xpcom-shutdown");

  delete sActiveWindows;
  sActiveWindows = nullptr;
}

nsresult
nsDOMCameraManager::Observe(nsISupports* aSubject, const char* aTopic, const char16_t* aData)
{
  if (strcmp(aTopic, "xpcom-shutdown") == 0) {
    XpComShutdown();
  }
  return NS_OK;
}

void
nsDOMCameraManager::OnNavigation(uint64_t aWindowId)
{
  DOM_CAMERA_LOGI(">>> OnNavigation event\n");
  Shutdown(aWindowId);
}

bool
nsDOMCameraManager::IsWindowStillActive(uint64_t aWindowId)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!sActiveWindows) {
    return false;
  }

  return !!sActiveWindows->Get(aWindowId);
}

JSObject*
nsDOMCameraManager::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return CameraManagerBinding::Wrap(aCx, this, aGivenProto);
}
#ifdef FEED_TEST_DATA_TO_PRODUCER
bool getYUVDataFromFile(const char *path,unsigned char * pYUVData,int size){
    FILE *fp = fopen(path,"rb");
    if(fp == NULL){
        return false;
    }
    fread(pYUVData,size,1,fp);
    fclose(fp);
    return true;
}

nsresult
nsDOMCameraManager::TestSurfaceInput()
{
  if (mTestANativeWindow == NULL) {
    if (mProducer != NULL) {
      mTestANativeWindow = new android::Surface(mProducer, /*controlledByApp*/ true);

      native_window_set_buffer_count(
              mTestANativeWindow.get(),
              8);
    } else {
      return NS_ERROR_FAILURE;
    }
  }

  //Prepare test data
  if (!mTestImage) {
    int size = 176 * 144  * 1.5;
    mTestImage = new unsigned char[size];

    const char *path = "/mnt/media_rw/sdcard/tulips_yuv420_prog_planar_qcif.yuv";
    bool getResult = getYUVDataFromFile(path, mTestImage, size);
    if (!getResult) {
      memset(mTestImage, 120, size);
    }  }


  if (!mTestImage2) {
    int size = 176 * 144  * 1.5;
    mTestImage2 = new unsigned char[size];

    const char *path = "/mnt/media_rw/sdcard/tulips_yvu420_inter_planar_qcif.yuv";
    bool getResult = getYUVDataFromFile(path, mTestImage2, size);//get yuv data from file;
    if (!getResult) {
      memset(mTestImage2, 60, size);
    }
  }

  mIsTestRunning = true;
  return TestSurfaceInputImpl();
}

nsresult
nsDOMCameraManager::TestSurfaceInputImpl()
{
  if (!mIsTestRunning) {
    return NS_OK;
  }

  int err;
  int cropWidth = 176;
  int cropHeight = 144;

  int halFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP;
  int bufWidth = (cropWidth + 1) & ~1;
  int bufHeight = (cropHeight + 1) & ~1;

  native_window_set_usage(
  mTestANativeWindow.get(),
  GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN
  | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP);


  native_window_set_scaling_mode(
  mTestANativeWindow.get(),
  NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);

  native_window_set_buffers_geometry(
      mTestANativeWindow.get(),
      bufWidth,
      bufHeight,
      halFormat);

  ANativeWindowBuffer *buf;

  if ((err = native_window_dequeue_buffer_and_wait(mTestANativeWindow.get(),
          &buf)) != 0) {
      return NS_OK;
  }

  GraphicBufferMapper &mapper = GraphicBufferMapper::get();

  android::Rect bounds(cropWidth, cropHeight);

  void *dst;
   mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst);

  if (mTestImageIndex == 0) {
    mTestImageIndex = 1;
    memcpy(dst, mTestImage, cropWidth * cropHeight * 1.5);
  } else {
    mTestImageIndex = 0;
    memcpy(dst, mTestImage2, cropWidth * cropHeight * 1.5);
  }
  mapper.unlock(buf->handle);

  err = mTestANativeWindow->queueBuffer(mTestANativeWindow.get(), buf, -1);

  buf = NULL;

  //Next round
  usleep(100000);
  nsCOMPtr<nsIRunnable> testSurfaceInputTask =
    NS_NewRunnableMethod(this, &nsDOMCameraManager::TestSurfaceInputImpl);
  NS_DispatchToMainThread(testSurfaceInputTask, NS_DISPATCH_NORMAL);

  return NS_OK;
}
#endif
