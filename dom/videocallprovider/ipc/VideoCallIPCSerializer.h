/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_videocallprovider_videocallprovideripcserializer_h__
#define mozilla_dom_videocallprovider_videocallprovideripcserializer_h__

#include "ipc/IPCMessageUtils.h"
#include "mozilla/dom/DOMVideoCallCameraCapabilities.h"
#include "mozilla/dom/DOMVideoCallProfile.h"
#include "nsIVideoCallProvider.h"
#include "nsIVideoCallCallback.h"

using mozilla::dom::DOMVideoCallProfile;
using mozilla::dom::DOMVideoCallCameraCapabilities;

typedef nsIVideoCallProfile* nsVideoCallProfile;
typedef nsIVideoCallCameraCapabilities* nsVideoCallCameraCapabilities;

namespace IPC {

/**
 * nsIVideoCallProfile Serialize/De-serialize.
 */
template <>
struct ParamTraits<nsIVideoCallProfile*>
{
  typedef nsIVideoCallProfile* paramType;

  // Function to serialize a DOMVideoCallProfile.
  static void Write(Message* aMsg, const paramType& aParam)
  {
    bool isNull = !aParam;
    WriteParam(aMsg, isNull);
    // If it is a null object, then we are don.
    if (isNull) {
      return;
    }

    uint16_t quality;
    uint16_t state;

    aParam->GetQuality(&quality);
    aParam->GetState(&state);

    WriteParam(aMsg, quality);
    WriteParam(aMsg, state);
    aParam->Release();
  }

  // Function to de-serialize a DOMVideoCallProfile.
  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    // Check if is the null pointer we have transferred.
    bool isNull;
    if (!ReadParam(aMsg, aIter, &isNull)) {
      return false;
    }

    if (isNull) {
      *aResult = nullptr;
    }

    uint16_t quality;
    uint16_t state;

    if (!(ReadParam(aMsg, aIter, &quality) &&
          ReadParam(aMsg, aIter, &state))) {
      return false;
    }

    *aResult = new DOMVideoCallProfile(quality, state);
    // We release this ref after receiver finishes processing.
    NS_ADDREF(*aResult);

    return true;
  }
};

/**
 * nsIVideoCallCameraCapabilities Serialize/De-serialize.
 */
template <>
struct ParamTraits<nsIVideoCallCameraCapabilities*>
{
  typedef nsIVideoCallCameraCapabilities* paramType;

  // Function to serialize a DOMVideoCallCameraCapabilities.
  static void Write(Message* aMsg, const paramType& aParam)
  {
    bool isNull = !aParam;
    WriteParam(aMsg, isNull);
    // If it is a null object, then we are don.
    if (isNull) {
      return;
    }

    uint16_t width;
    uint16_t height;
    bool zoomSupported;
    uint16_t maxZoom;

    aParam->GetWidth(&width);
    aParam->GetHeight(&height);
    aParam->GetZoomSupported(&zoomSupported);
    aParam->GetMaxZoom(&maxZoom);

    WriteParam(aMsg, width);
    WriteParam(aMsg, height);
    WriteParam(aMsg, zoomSupported);
    WriteParam(aMsg, maxZoom);
    aParam->Release();
  }

  // Function to de-serialize a DOMVideoCallCameraCapabilities.
  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    // Check if is the null pointer we have transferred.
    bool isNull;
    if (!ReadParam(aMsg, aIter, &isNull)) {
      return false;
    }

    if (isNull) {
      *aResult = nullptr;
    }

    uint16_t width;
    uint16_t height;
    bool zoomSupported;
    uint16_t maxZoom;

    if (!(ReadParam(aMsg, aIter, &width) &&
          ReadParam(aMsg, aIter, &height) &&
          ReadParam(aMsg, aIter, &zoomSupported) &&
          ReadParam(aMsg, aIter, &maxZoom))) {
      return false;
    }

    *aResult = new DOMVideoCallCameraCapabilities(width, height, zoomSupported, maxZoom);
    // We release this ref after receiver finishes processing.
    NS_ADDREF(*aResult);

    return true;
  }
};

} // namespace IPC

#endif // mozilla_dom_videocallprovider_videocallprovideripcserializer_h__