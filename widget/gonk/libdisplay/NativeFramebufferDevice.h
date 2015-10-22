/* Copyright 2013 Mozilla Foundation and Mozilla contributors
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

#ifndef NATIVEFRAMEBUFFERDEVICE_H
#define NATIVEFRAMEBUFFERDEVICE_H

#include <hardware/gralloc.h>
#include <linux/fb.h>
#include <system/window.h>

namespace mozilla {

class NativeFramebufferDevice {
public:
    NativeFramebufferDevice();

    ~NativeFramebufferDevice();

    bool Open(const char* deviceName);

    bool Post(buffer_handle_t buf);

    bool Close();

    bool PowerOnBackLight();

    bool PowerOffBackLight();

    bool IsValid();

public:
    // Only be valid after open sucessfully
    uint32_t mWidth;
    uint32_t mHeight;
    int32_t mSurfaceformat;
    float mXdpi;

private:
    int mFd;
    struct fb_var_screeninfo mVInfo;
    struct fb_fix_screeninfo mFInfo;
    gralloc_module_t *mGrmodule;
    int32_t mFBSurfaceformat;
};

}

#endif /* NATIVEFRAMEBUFFERDEVICE_H */
