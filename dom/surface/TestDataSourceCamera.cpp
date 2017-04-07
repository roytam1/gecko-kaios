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

#include "TestDataSourceCamera.h"

#include <binder/IPCThreadState.h>
#include <sys/system_properties.h>
#include <binder/IServiceManager.h>
#include <camera/CameraParameters.h>

#ifdef MY_LOG
#undef MY_LOG
#endif
#define MY_LOG(args...) __android_log_print(ANDROID_LOG_INFO, "TestDataSourceCamera", ##args)


using namespace android;

TestDataSourceCamera::TestDataSourceCamera(ITestDataSourceResolutionResultListener* aResolutionResultListener)
  : mTestDataSourceResolutionResultListener(aResolutionResultListener)
  , mPreferPreviewWidth(0)
  , mPreferPreviewHeight(0)
  , mResultPreviewWidth(176)
  , mResultPreviewHeight(144)
  , mPreviewStarted(false)
  , mTestImage(NULL)
  , mTestImage2(NULL)
  , mTestImageIndex(0)
  , mIsTestRunning(false)
  , mPreferDisplayWidth(0)
  , mPreferDisplayHeight(0)
  , mResultDisplayWidth(176)
  , mResultDisplayHeight(144)
  , mDisplayStarted(false)
{
  MY_LOG("TestDataSourceCamera()");
}

void 
TestDataSourceCamera::SetPreviewSurface(android::sp<android::IGraphicBufferProducer>& aProducer,
                                        uint32_t aPreferWidth,
                                        uint32_t aPreferHeight)
{
  MY_LOG("SetPreviewSurface aPreferWidth:%d, aPreferHeight:%d, mResultPreviewWidth:%d, mResultPreviewHeight:%d", 
    aPreferWidth, aPreferHeight, mResultPreviewWidth, mResultPreviewHeight);

  if (mCamera == NULL) {
    const uint32_t CAMERASERVICE_POLL_DELAY = 1000000;
    const uint32_t DEFAULT_USER_ID  = 0;

    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder;
    sp<ICameraService> gCameraService;
    do {
      binder = sm->getService(String16("media.camera"));
      if (binder != 0) {
        break;
      }
      MY_LOG("CameraService not published, waiting...");
      usleep(CAMERASERVICE_POLL_DELAY);
    } while(true);

    gCameraService = interface_cast<ICameraService>(binder);
    int32_t args[1];
    args[0] = DEFAULT_USER_ID;
    int32_t event = 1;
    size_t length = 1;

    gCameraService->notifySystemEvent(event, args, length);
    mCamera = Camera::connect(0, /* clientPackageName */String16("gonk.camera"), Camera::USE_CALLING_UID);
  }

  if (mCamera != NULL) {
      mCamera->setPreviewTarget(aProducer);
      mPreferPreviewWidth = aPreferWidth;
      mPreferPreviewHeight = aPreferHeight;
      if(mTestDataSourceResolutionResultListener) {
        MY_LOG("onChangeCameraCapabilities");
        mTestDataSourceResolutionResultListener->onChangeCameraCapabilities(mResultPreviewWidth, mResultPreviewHeight);
      }
  } else {
    MY_LOG("SetPreviewSurface without valid camera");
  }
}

void 
TestDataSourceCamera::SetDisplaySurface(android::sp<android::IGraphicBufferProducer>& aProducer,
                                        uint32_t aPreferWidth,
                                        uint32_t aPreferHeight)
{
  MY_LOG("SetDisplaySurface aPreferWidth:%d, aPreferHeight:%d, mResultPreviewWidth:%d, mResultPreviewHeight:%d", 
    aPreferWidth, aPreferHeight, mResultPreviewWidth, mResultPreviewHeight);
  //Do nothing, since we are TestDataSource"Camera", can only open 1 camera a time.
  mFakeImageProducer = aProducer;
  if(mTestDataSourceResolutionResultListener) {
    mTestDataSourceResolutionResultListener->onChangePeerDimensions(176, 144);
  }
}

void 
TestDataSourceCamera::SetCameraParameters()
{
  MY_LOG("SetCameraParameters");
  if (mCamera != NULL) {  
    CameraParameters params;
    // Initialize our camera configuration database.
    const String8 s1 = mCamera->getParameters();
    params.unflatten(s1);
    // Set preferred preview frame format.
    params.setPreviewFormat("yuv420sp");
    MY_LOG("PushParameters: ResultWidth:%d ResultHeight:%d", mResultPreviewWidth, mResultPreviewHeight);
    params.setPreviewSize(mResultPreviewWidth, mResultPreviewHeight);
    // Set parameter back to camera.
    String8 s2 = params.flatten();
    MY_LOG("PushParameters:%s Line:%d", s2.string(), __LINE__);
    mCamera->setParameters(s2);
  } else {
    MY_LOG("SetCameraParameters without valid camera");
  }
}

void TestDataSourceCamera::Start()
{
  MY_LOG("Start");
  if (mCamera != NULL && !mPreviewStarted) {
    SetCameraParameters();
    MY_LOG("startPreview");
    mCamera->startPreview();
    mPreviewStarted = true;
  }

  if (mFakeImageProducer != NULL) {
    if (mFakeImageFeederLooper == NULL) {
      mFakeImageFeederLooper = new ALooper;MY_LOG("line:%d", __LINE__);
      mFakeImageFeeder = new FakeImageFeeder(this);MY_LOG("line:%d", __LINE__);
      mFakeImageFeederLooper->registerHandler(mFakeImageFeeder);MY_LOG("line:%d", __LINE__);
      mFakeImageFeederLooper->setName("FakeImageLooper");MY_LOG("line:%d", __LINE__);
      mFakeImageFeederLooper->start(  
         false, // runOnCallingThread  
         false, // canCallJava  
         ANDROID_PRIORITY_AUDIO);
    }
    StartFakeImage();
  }
}

void TestDataSourceCamera::Stop()
{
  MY_LOG("Stop");
  //Stop Preview
  if (mCamera != NULL) {
    mCamera->stopPreview();
  }

  //Stop Display
  mIsTestRunning = false;
}

TestDataSourceCamera::~TestDataSourceCamera()
{
  MY_LOG("~TestDataSourceCamera");
  if (mCamera != NULL) {
    mCamera->disconnect();
    mCamera.clear();
  }

  if (mTestImage) {
    delete [] mTestImage;
    mTestImage = NULL;
  }

  if (mTestImage2) {
    delete [] mTestImage2;
    mTestImage2 = NULL;
  }
}

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
TestDataSourceCamera::StartFakeImage()
{
  if (mTestANativeWindow == NULL) {
    if (mFakeImageProducer != NULL) {
      mTestANativeWindow = new android::Surface(mFakeImageProducer, /*controlledByApp*/ true);

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

  sp<AMessage> msg = new AMessage(FakeImageFeeder::kWhatSendFakeImage, mFakeImageFeeder);
  msg->post();

  return NS_OK;
}

FakeImageFeeder::FakeImageFeeder(TestDataSourceCamera* aTestDataSourceCamera)
  : mTestDataSourceCamera(aTestDataSourceCamera)
{
}

FakeImageFeeder::~FakeImageFeeder()
{
}

void 
FakeImageFeeder::onMessageReceived(const sp<AMessage> &msg)
{
  //MY_LOG("onMessageReceived");

    switch (msg->what()) {
        case kWhatSendFakeImage:
        {
            SendFakeImage(mTestDataSourceCamera);
            break;
        }
        default:
          //Do nothing
          return;
    }
}

void
FakeImageFeeder::SendFakeImage(TestDataSourceCamera* aTestDataSourceCamera)
{
  //MY_LOG("SendFakeImage");
  if (!aTestDataSourceCamera->mIsTestRunning) {
    return;
  }

  int err;  
  int cropWidth = 176;  
  int cropHeight = 144;  
    
  int halFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP;
  int bufWidth = (cropWidth + 1) & ~1;
  int bufHeight = (cropHeight + 1) & ~1;  

  native_window_set_usage(  
    aTestDataSourceCamera->mTestANativeWindow.get(),  
    GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN  
    | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP);  


  native_window_set_scaling_mode(  
    aTestDataSourceCamera->mTestANativeWindow.get(),  
    NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);  

  native_window_set_buffers_geometry(  
      aTestDataSourceCamera->mTestANativeWindow.get(),  
      bufWidth,  
      bufHeight,  
      halFormat);

  ANativeWindowBuffer *buf; 

  if ((err = native_window_dequeue_buffer_and_wait(aTestDataSourceCamera->mTestANativeWindow.get(),  
          &buf)) != 0) {
      return;  
  }

  GraphicBufferMapper &mapper = GraphicBufferMapper::get();  

  android::Rect bounds(cropWidth, cropHeight);  

  void *dst;
  mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst);

  if (aTestDataSourceCamera->mTestImageIndex == 0) {
    aTestDataSourceCamera->mTestImageIndex = 1;
    memcpy(dst, aTestDataSourceCamera->mTestImage, cropWidth * cropHeight * 1.5);
  } else {
    aTestDataSourceCamera->mTestImageIndex = 0;
    memcpy(dst, aTestDataSourceCamera->mTestImage2, cropWidth * cropHeight * 1.5);
  }
  mapper.unlock(buf->handle);  

  err = aTestDataSourceCamera->mTestANativeWindow->queueBuffer(aTestDataSourceCamera->mTestANativeWindow.get(), buf, -1);

  buf = NULL;  

  //Next round
  usleep(100000);
  sp<AMessage> msg = new AMessage(kWhatSendFakeImage, this);
  msg->post();
}
