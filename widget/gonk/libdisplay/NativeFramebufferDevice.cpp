/* Copyright (C) 2015 Acadine Technologies. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "NativeFramebufferDevice.h"
#include "mozilla/FileUtils.h"
#include "utils/Log.h"

#define DEFAULT_XDPI 75.0

namespace mozilla {

// Device file could be different among platforms
// Need to find a way to set this by build or by android properties
static const char* kBacklightDevice = "/sys/class/ktd20xx/ktd2026/back_light_led";


inline unsigned int roundUpToPageSize(unsigned int x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

inline bool WriteValueToFile(const char* name, const char* value)
{
    if (!name || !value)
        return false;

    mozilla::ScopedClose fdLight(open(name, O_RDWR));

    if (fdLight.get() == -1) {
        ALOGE("Failed to open backlight device.");
        return false;
    }

    if (lseek(fdLight.get(), 0, SEEK_SET) >= 0) {
        write(fdLight.get(), value, 4);
    }

    return true;
}

#ifdef BUILD_ARM_NEON

#define RBGATORGB565                                                           \
    "vshr.u8    d20, d20, #3                   \n"  /* R                    */ \
    "vshr.u8    d21, d21, #2                   \n"  /* G                    */ \
    "vshr.u8    d22, d22, #3                   \n"  /* B                    */ \
    "vmovl.u8   q8, d20                        \n"  /* R                    */ \
    "vmovl.u8   q9, d21                        \n"  /* G                    */ \
    "vmovl.u8   q10, d22                       \n"  /* B                    */ \
    "vshl.u16   q8, q8, #11                    \n"  /* R                    */ \
    "vshl.u16   q9, q9, #5                     \n"  /* G                    */ \
    "vorr       q0, q8, q9                     \n"  /* RG                   */ \
    "vorr       q0, q0, q10                    \n"  /* RGB                  */

inline void Transform8888To565_NEON(uint8_t* outbuf, const uint8_t* inbuf, int PixelNum) {
  asm volatile (
    ".p2align   2                              \n"
  "1:                                          \n"
    "vld4.8     {d20, d21, d22, d23}, [%0]!    \n"  // load 8 pixels of ARGB.
    "subs       %2, %2, #8                     \n"  // 8 processed per loop.
    RBGATORGB565
    "vst1.8     {q0}, [%1]!                    \n"  // store 8 pixels RGB565.
    "bgt        1b                             \n"
  : "+r"(inbuf),    // %0
    "+r"(outbuf),   // %1
    "+r"(PixelNum)  // %2
  :
  : "cc", "memory", "q0", "q8", "q9", "q10"
  );
}

#else
inline void Transform8888To565_Software(uint8_t* outbuf, const uint8_t* inbuf, int PixelNum)
{
    uint32_t bytes = PixelNum * 4;
    uint16_t *out = (uint16_t *)outbuf;
    uint8_t *in = (uint8_t *)inbuf;
    for (uint32_t i = 0; i < bytes; i += 4) {
        *out++ = ((in[i]     & 0xF8) << 8) |
                 ((in[i + 1] & 0xFC) << 3) |
                 ((in[i + 2]       ) >> 3);
    }
}

#endif

inline void Transform8888To565(uint8_t* outbuf, const uint8_t* inbuf, int PixelNum)
{
#ifdef BUILD_ARM_NEON
        Transform8888To565_NEON(outbuf, inbuf, PixelNum);
#else
        Transform8888To565_Software(outbuf, inbuf, PixelNum);
#endif
}

NativeFramebufferDevice::NativeFramebufferDevice()
    : mWidth(320)
    , mHeight(480)
    , mSurfaceformat(HAL_PIXEL_FORMAT_RGBA_8888)
    , mXdpi(DEFAULT_XDPI)
    , mFd(-1)
    , mGrmodule(nullptr)
{
}

NativeFramebufferDevice::~NativeFramebufferDevice()
{
    Close();
}

bool
NativeFramebufferDevice::Open(const char* deviceName)
{
    if (!deviceName) {
        return false;
    }

    char const *const device_template[] = {
            "/dev/graphics/%s",
            "/dev/fb%s",
            0 };

    int i=0;
    char name[64];

    while ((mFd == -1) && device_template[i]) {
        snprintf(name, 64, device_template[i], deviceName);
        mFd = open(name, O_RDWR, 0);
        i++;
    }

    if (mFd < 0) {
        ALOGE("Fail to open framebuffer device %s", deviceName);
        return false;
    }

    if (ioctl(mFd, FBIOGET_FSCREENINFO, &mFInfo) == -1) {
        ALOGE("FBIOGET_FSCREENINFO failed with %s", deviceName);
        Close();
        return false;
    }

    if (ioctl(mFd, FBIOGET_VSCREENINFO, &mVInfo) == -1) {
        ALOGE("FBIOGET_VSCREENINFO: failed with %s", deviceName);
        Close();
        return false;
    }

    mVInfo.reserved[0] = 0;
    mVInfo.reserved[1] = 0;
    mVInfo.reserved[2] = 0;
    mVInfo.xoffset = 0;
    mVInfo.yoffset = 0;
    mVInfo.activate = FB_ACTIVATE_NOW;

    if(mVInfo.bits_per_pixel == 32) {

        // Explicitly request RGBA_8888
        mVInfo.bits_per_pixel = 32;
        mVInfo.red.offset     = 24;
        mVInfo.red.length     = 8;
        mVInfo.green.offset   = 16;
        mVInfo.green.length   = 8;
        mVInfo.blue.offset    = 8;
        mVInfo.blue.length    = 8;
        mVInfo.transp.offset  = 0;
        mVInfo.transp.length  = 8;

        mFBSurfaceformat = HAL_PIXEL_FORMAT_RGBX_8888;
    } else {

        // Explicitly request 5/6/5
        mVInfo.bits_per_pixel = 16;
        mVInfo.red.offset     = 11;
        mVInfo.red.length     = 5;
        mVInfo.green.offset   = 5;
        mVInfo.green.length   = 6;
        mVInfo.blue.offset    = 0;
        mVInfo.blue.length    = 5;
        mVInfo.transp.offset  = 0;
        mVInfo.transp.length  = 0;

        mFBSurfaceformat = HAL_PIXEL_FORMAT_RGB_565;
    }

    if (ioctl(mFd, FBIOPUT_VSCREENINFO, &mVInfo) == -1) {
        ALOGW("FBIOPUT_VSCREENINFO failed, update offset failed");
        Close();
        return false;
    }

    if (int(mVInfo.width) <= 0 || int(mVInfo.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        mVInfo.width  = ((mVInfo.xres * 25.4f)/160.0f + 0.5f);
        mVInfo.height = ((mVInfo.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = (mVInfo.xres * 25.4f) / mVInfo.width;
    float ydpi = (mVInfo.yres * 25.4f) / mVInfo.height;

    ALOGI(  "using (fd=%d)\n"
            "id           = %s\n"
            "xres         = %d px\n"
            "yres         = %d px\n"
            "xres_virtual = %d px\n"
            "yres_virtual = %d px\n"
            "bpp          = %d\n"
            "r            = %2u:%u\n"
            "g            = %2u:%u\n"
            "b            = %2u:%u\n",
            mFd,
            mFInfo.id,
            mVInfo.xres,
            mVInfo.yres,
            mVInfo.xres_virtual,
            mVInfo.yres_virtual,
            mVInfo.bits_per_pixel,
            mVInfo.red.offset, mVInfo.red.length,
            mVInfo.green.offset, mVInfo.green.length,
            mVInfo.blue.offset, mVInfo.blue.length
    );

    ALOGI(  "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n",
            mVInfo.width,  xdpi,
            mVInfo.height, ydpi
    );

    const hw_module_t *module = nullptr;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module)) {
        ALOGE("Could not get gralloc module");
        Close();
        return false;
    }

    mGrmodule = const_cast<gralloc_module_t *>(reinterpret_cast<const gralloc_module_t *>(module));

    mWidth = mVInfo.xres;
    mHeight = mVInfo.yres;
    mXdpi = xdpi;
    mSurfaceformat = HAL_PIXEL_FORMAT_RGBA_8888;

    return true;
}

bool
NativeFramebufferDevice::Post(buffer_handle_t buf)
{
    uint32_t screensize = roundUpToPageSize(mFInfo.line_length * mVInfo.yres_virtual);

    void *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
    if (fbp == (void*)-1) {
        ALOGE("Error: failed to map framebuffer device to memory %d : %s", errno, strerror(errno));
        return false;
    }

    void *vaddr;
    if (mGrmodule->lock(mGrmodule, buf,
                        GRALLOC_USAGE_SW_READ_RARELY,
                        0, 0, mVInfo.xres, mVInfo.yres, &vaddr)) {
        ALOGE("Failed to lock buffer_handle_t");
        munmap(fbp, screensize);
        return false;
    }

    if (mFBSurfaceformat == HAL_PIXEL_FORMAT_RGB_565) {
        Transform8888To565((uint8_t*)fbp, (uint8_t*)vaddr, mVInfo.xres*mVInfo.yres);
    } else {
        memcpy(fbp, vaddr, mFInfo.line_length * mVInfo.yres);
    }

    mGrmodule->unlock(mGrmodule, buf);

    mVInfo.activate = FB_ACTIVATE_VBL;

    if(0 > ioctl(mFd, FBIOPUT_VSCREENINFO, &mVInfo)) {
      ALOGE("FBIOPUT_VSCREENINFO failed : error on refresh");
    }

    munmap(fbp, screensize);

    return true;
}

bool
NativeFramebufferDevice::IsValid()
{
    return mFd != -1;
}

bool
NativeFramebufferDevice::Close()
{
    if (mFd != -1) {
        close(mFd);
        mFd = -1;
    }

    return true;
}

bool
NativeFramebufferDevice::PowerOnBackLight()
{
    return WriteValueToFile(kBacklightDevice, "255");
}

bool
NativeFramebufferDevice::PowerOffBackLight()
{
    return WriteValueToFile(kBacklightDevice, "0");
}

} // namespace mozilla
