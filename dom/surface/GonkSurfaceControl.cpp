/*
 * Copyright (C) 2012-2015 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "GonkSurfaceControl.h"
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>
#include "base/basictypes.h"
#include "Layers.h"
#ifdef MOZ_WIDGET_GONK
#include "GrallocImages.h"
#include "imgIEncoder.h"
#include "libyuv.h"
#include "nsNetUtil.h" // for NS_ReadInputStreamToBuffer
#endif
#include "nsNetCID.h" // for NS_STREAMTRANSPORTSERVICE_CONTRACTID
#include "nsAutoPtr.h" // for nsAutoArrayPtr
#include "nsCOMPtr.h"
#include "nsMemory.h"
#include "nsThread.h"
#include "nsITimer.h"
#include "mozilla/FileUtils.h"
#include "mozilla/Services.h"
#include "mozilla/unused.h"
#include "mozilla/ipc/FileDescriptorUtils.h"
#include "nsAlgorithm.h"
#include "nsPrintfCString.h"
#include "GonkCameraHwMgr.h"
#include <gui/IGraphicBufferProducer.h>
#include <gui/Surface.h>
#include <android/native_window.h>
#include <ui/GraphicBufferMapper.h>
#include <system/graphics.h>
#include <cutils/properties.h>

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::layers;
using namespace mozilla::gfx;
using namespace mozilla::ipc;
using namespace android;

static const unsigned long kAutoFocusCompleteTimeoutMs = 1000;
static const int32_t kAutoFocusCompleteTimeoutLimit = 3;

// Construct nsGonkSurfaceControl on the main thread.
nsGonkSurfaceControl::nsGonkSurfaceControl()
#ifdef FEED_TEST_DATA_TO_PRODUCER
  : mTestImage(NULL)
  , mTestImage2(NULL)
  , mTestImageIndex(0)
  , mIsTestRunning(false)
#endif
{
  // Constructor runs on the main thread...
  mImageContainer = LayerManager::CreateImageContainer();
}

nsresult
nsGonkSurfaceControl::Initialize()
{
  if (mSurfaceHw.get()) {
    DOM_CAMERA_LOGI("Surface already connected (this=%p)\n", this);
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  mSurfaceHw = GonkSurfaceHardware::Connect(this);
  if (!mSurfaceHw.get()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  return NS_OK;
}

nsGonkSurfaceControl::~nsGonkSurfaceControl()
{
  if (mTestImage) {
    delete [] mTestImage;
  }

  if (mTestImage2) {
    delete [] mTestImage2;
  }
}

nsresult
nsGonkSurfaceControl::SetConfigurationInternal(const Configuration& aConfig)
{
  mCurrentConfiguration.mPreviewSize.width = aConfig.mPreviewSize.width;
  mCurrentConfiguration.mPreviewSize.height = aConfig.mPreviewSize.height;

  return NS_OK;
}

nsresult
nsGonkSurfaceControl::StartImpl(const Configuration* aInitialConfig)
{
  MOZ_ASSERT(NS_GetCurrentThread() == mSurfaceThread);

  nsresult rv = StartInternal(aInitialConfig);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    android::sp<android::IGraphicBufferProducer> empty;
    OnSurfaceStateChange(SurfaceControlListener::kSurfaceCreateFailed,
                         NS_ERROR_NOT_AVAILABLE, empty);
  }
  return rv;
}

nsresult
nsGonkSurfaceControl::StartInternal(const Configuration* aInitialConfig)
{
  nsresult rv = Initialize();
  switch (rv) {
    case NS_ERROR_ALREADY_INITIALIZED:
    case NS_OK:
      break;

    default:
      return rv;
  }

  if (aInitialConfig) {
    rv = SetConfigurationInternal(*aInitialConfig);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      // The initial configuration failed, close up the hardware
      StopInternal();
      return rv;
    }
  }
  android::sp<IGraphicBufferProducer> producer = mSurfaceHw->GetGraphicBufferProducer();
  OnSurfaceStateChange(SurfaceControlListener::kSurfaceCreate, NS_OK, producer);

  return NS_OK;
}

nsresult
nsGonkSurfaceControl::StopInternal()
{
#ifdef FEED_TEST_DATA_TO_PRODUCER
  //Stop test if any
  mIsTestRunning = false;
#endif

  // release the surface handle
  if (mSurfaceHw.get()){
     mSurfaceHw->Close();
     mSurfaceHw.clear();
  }

  return NS_OK;
}

nsresult
nsGonkSurfaceControl::StopImpl()
{

  nsresult rv = StopInternal();
  if (rv != NS_ERROR_NOT_INITIALIZED) {
    rv = NS_OK;
  }
  if (NS_SUCCEEDED(rv)) {
    android::sp<android::IGraphicBufferProducer> empty;
    OnSurfaceStateChange(SurfaceControlListener::kSurfaceDestroyed, NS_OK, empty);
  }
  return rv;
}

nsresult
nsGonkSurfaceControl::StartPreviewInternal()
{
  MOZ_ASSERT(NS_GetCurrentThread() == mSurfaceThread);

//  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mPreviewState == SurfaceControlListener::kPreviewStarted) {
    return NS_OK;
  }

  return NS_OK;
}

nsresult
nsGonkSurfaceControl::StartPreviewImpl()
{
  nsresult rv = StartPreviewInternal();
  if (NS_SUCCEEDED(rv)) {
    OnPreviewStateChange(SurfaceControlListener::kPreviewStarted);
  }

#ifdef FEED_TEST_DATA_TO_PRODUCER
  char prop[128];
    if (property_get("vt.surface.test", prop, NULL) != 0) {
      if (strcmp(prop, "1") == 0) {
        TestSurfaceInput();
      }
  }
#endif

  return rv;
}

#ifdef FEED_TEST_DATA_TO_PRODUCER
bool getYUVData(const char *path,unsigned char * pYUVData,int size){
    FILE *fp = fopen(path,"rb");
    if(fp == NULL){
        return false;
    }
    fread(pYUVData,size,1,fp);
    fclose(fp);
    return true;
}

nsresult
nsGonkSurfaceControl::TestSurfaceInput()
{

  MOZ_ASSERT(NS_GetCurrentThread() == mSurfaceThread);

  if (mSurfaceHw == NULL) {
    return NS_ERROR_FAILURE;
  }

  if (mTestANativeWindow == NULL) {
    android::sp<IGraphicBufferProducer> producer = mSurfaceHw->GetGraphicBufferProducer();
    if (producer != NULL) {
      mTestANativeWindow = new android::Surface(producer, /*controlledByApp*/ true);

          native_window_set_buffer_count(
                  mTestANativeWindow.get(),
                  8);
    } else {
      return NS_ERROR_FAILURE;
    }
  }

  //Prepare test data
  if (!mTestImage) {
    int size = mCurrentConfiguration.mPreviewSize.width * mCurrentConfiguration.mPreviewSize.height  * 1.5;
    mTestImage = new unsigned char[size];

    const char *path = "/mnt/media_rw/sdcard/tulips_yuv420_prog_planar_qcif.yuv";
    bool getResult = getYUVData(path, mTestImage, size);//get yuv data from file;
    if (!getResult) {
      memset(mTestImage, 120, size);
    }
  }


  if (!mTestImage2) {
    int size = mCurrentConfiguration.mPreviewSize.width * mCurrentConfiguration.mPreviewSize.height  * 1.5;
    mTestImage2 = new unsigned char[size];

    const char *path = "/mnt/media_rw/sdcard/tulips_yvu420_inter_planar_qcif.yuv";
    bool getResult = getYUVData(path, mTestImage2, size);//get yuv data from file;
    if (!getResult) {
      memset(mTestImage2, 60, size);
    }
  }

  mIsTestRunning = true;

  /**
   * If we're already on the surface thread, call
   * TestSurfaceInputImpl() directly, so that it executes
   * synchronously.  Some callers require this so that changes
   * take effect immediately before we can proceed.
   */
  if (NS_GetCurrentThread() != mSurfaceThread) {
    nsCOMPtr<nsIRunnable> testSurfaceInputTask =
      NS_NewRunnableMethod(this, &nsGonkSurfaceControl::TestSurfaceInputImpl);
    return mSurfaceThread->Dispatch(testSurfaceInputTask, NS_DISPATCH_NORMAL);
  }

  return TestSurfaceInputImpl();
}

nsresult
nsGonkSurfaceControl::TestSurfaceInputImpl()
{

  if (!mIsTestRunning) {
    return NS_OK;
  }

  int err;
  int cropWidth = mCurrentConfiguration.mPreviewSize.width;
  int cropHeight = mCurrentConfiguration.mPreviewSize.height;

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
    NS_NewRunnableMethod(this, &nsGonkSurfaceControl::TestSurfaceInputImpl);
  mSurfaceThread->Dispatch(testSurfaceInputTask, NS_DISPATCH_NORMAL);

  return NS_OK;
}
#endif

void
nsGonkSurfaceControl::OnNewPreviewFrame(layers::TextureClient* aBuffer)
{
#ifdef MOZ_WIDGET_GONK
  RefPtr<GrallocImage> frame = new GrallocImage();

  IntSize picSize(mCurrentConfiguration.mPreviewSize.width,
                  mCurrentConfiguration.mPreviewSize.height);
  frame->AdoptData(aBuffer, picSize);
/*
  if (mCapturePoster.exchange(false)) {
    CreatePoster(frame,
                 mCurrentConfiguration.mPreviewSize.width,
                 mCurrentConfiguration.mPreviewSize.height,
                 mVideoRotation);
    return;
  }*/

  OnNewPreviewFrame(frame, mCurrentConfiguration.mPreviewSize.width,
                    mCurrentConfiguration.mPreviewSize.height);
#endif
}

// Gonk callback handlers.
namespace mozilla {

void
OnNewPreviewFrame(nsGonkSurfaceControl* gc, layers::TextureClient* aBuffer)
{
  gc->OnNewPreviewFrame(aBuffer);
}

} // namespace mozilla