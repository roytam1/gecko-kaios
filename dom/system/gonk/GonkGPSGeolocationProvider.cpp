/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* Copyright 2012 Mozilla Foundation and Mozilla contributors
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

#include "GonkGPSGeolocationProvider.h"

#include <cmath>
#include <pthread.h>
#include <hardware/gps.h>
#include <hardware_legacy/power.h>
#include <sys/stat.h>

#include "GeolocationUtil.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "nsContentUtils.h"
#include "nsGeoPosition.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsINetworkInterface.h"
#include "nsIObserverService.h"
#include "nsIURLFormatter.h"
#include "nsJSUtils.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "prtime.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SettingChangeNotificationBinding.h"

#ifdef MOZ_B2G_RIL
#include "mozstumbler/MozStumbler.h"
#include "nsIIccInfo.h"
#include "nsIMobileConnectionInfo.h"
#include "nsIMobileConnectionService.h"
#include "nsIMobileCellInfo.h"
#include "nsIMobileNetworkInfo.h"
#include "nsIRadioInterfaceLayer.h"
#include "nsIIccService.h"
#include "nsIDataCallManager.h"
#endif

#ifdef HAS_KOOST_MODULES
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/PContentParent.h" // for SendGnssNmeaUpdate
#include "mozilla/unused.h"
#endif

#define FLUSH_AIDE_DATA 0
#define SUPL_NI_NOTIFY  "supl-ni-notify"
#define SUPL_NI_VERIFY  "supl-ni-verify"
#define SUPL_NI_VERIFY_TIMEOUT  "supl-ni-verify-timeout"
/* This flag is for sending chrome event to close the pop-up window.
   The value is determined here which should not be used by GpsNiNotifyFlags.
   GPS_NI_NEED_NOTIFY      (0x0001)
   GPS_NI_NEED_VERIFY      (0x0002)
   GPS_NI_PRIVACY_OVERRIDE (0x0004) */
#define GPS_NI_NEED_TIMEOUT 0xFFFF

#undef LOG
#undef ERR
#undef DBG
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO,  "GonkGPS_GEO", ## args)
#define ERR(args...)  __android_log_print(ANDROID_LOG_ERROR, "GonkGPS_GEO", ## args)
#define DBG(args...)                                                   \
  do {                                                                 \
    if (gDebug_isLoggingEnabled) {                                     \
      __android_log_print(ANDROID_LOG_DEBUG, "GonkGPS_GEO", ## args);  \
    }                                                                  \
  } while(0)

using namespace mozilla;
using namespace mozilla::dom;

static const int kDefaultPeriod = 1000; // ms
static bool gDebug_isLoggingEnabled = false;
static bool gDebug_isGPSLocationIgnored = false;
static bool gHasAuthorizationKey = false;

#ifdef MOZ_B2G_RIL
static const char* kNetworkConnStateChangedTopic = "network-connection-state-changed";
#endif
static const char* kMozSettingsChangedTopic = "mozsettings-changed";
#ifdef MOZ_B2G_RIL
static const char* kPrefRilNumRadioInterfaces = "ril.numRadioInterfaces";
static const char* kSettingRilDefaultServiceId = "ril.data.defaultServiceId";
#endif

// Clean up GPS HAL when Geolocation setting is turned off.
static const char* kPrefOndemandCleanup = "geo.provider.ondemand_cleanup";

// The geolocation enabled setting
static const char* kSettingGeolocationEnabled = "geolocation.enabled";

// Both of these settings can be toggled in the Gaia Developer settings screen.
static const char* kSettingDebugEnabled = "geolocation.debugging.enabled";
static const char* kSettingDebugGpsIgnored = "geolocation.debugging.gps-locations-ignored";
// Gaia will modify the value of settings key(supl.verification.choice)
//   when user make his/her choice of SUPL NI verification.
static const char* kSettingSuplVerificationChoice = "supl.verification.choice";
static const char* kWakeLockName = "GeckoGPS";


// While most methods of GonkGPSGeolocationProvider should only be
// called from main thread, we deliberately put the Init and ShutdownGPS
// methods off main thread to avoid blocking.
NS_IMPL_ISUPPORTS(GonkGPSGeolocationProvider,
                  nsIGeolocationProvider,
                  nsIObserver,
                  nsISettingsServiceCallback)

/* static */ GonkGPSGeolocationProvider* GonkGPSGeolocationProvider::sSingleton = nullptr;
GpsCallbacks GonkGPSGeolocationProvider::mCallbacks;
GpsNiCallbacks GonkGPSGeolocationProvider::mGPSNiCallbacks;
// This array is used for recording the request_id of replied SUPL NI request.
// In "TimeoutResponseEvent", we will check this array before replying the
// default response.
static nsTArray<int> repliedSuplNiReqIds;

#ifdef MOZ_B2G_RIL
AGpsCallbacks GonkGPSGeolocationProvider::mAGPSCallbacks;
AGpsRilCallbacks GonkGPSGeolocationProvider::mAGPSRILCallbacks;
#endif // MOZ_B2G_RIL

void
GonkGPSGeolocationProvider::LocationCallback(GpsLocation* location)
{
  if (gDebug_isGPSLocationIgnored) {
    return;
  }

  class UpdateLocationEvent : public nsRunnable {
  public:
    UpdateLocationEvent(nsGeoPosition* aPosition)
      : mPosition(aPosition)
    {}
    NS_IMETHOD Run() {
      RefPtr<GonkGPSGeolocationProvider> provider =
        GonkGPSGeolocationProvider::GetSingleton();
      nsCOMPtr<nsIGeolocationUpdate> callback = provider->mLocationCallback;
      provider->mLastGPSPosition = mPosition;
      if (callback) {
        callback->Update(mPosition);
      }
      return NS_OK;
    }
  private:
    RefPtr<nsGeoPosition> mPosition;
  };

  MOZ_ASSERT(location);

  const float kImpossibleAccuracy_m = 0.001;
  if (location->accuracy < kImpossibleAccuracy_m) {
    return;
  }

  RefPtr<nsGeoPosition> somewhere = new nsGeoPosition(location->latitude,
                                                        location->longitude,
                                                        location->altitude,
                                                        location->accuracy,
                                                        location->accuracy,
                                                        location->bearing,
                                                        location->speed,
                                                        PR_Now() / PR_USEC_PER_MSEC);
  // Note above: Can't use location->timestamp as the time from the satellite is a
  // minimum of 16 secs old (see http://leapsecond.com/java/gpsclock.htm).
  // All code from this point on expects the gps location to be timestamped with the
  // current time, most notably: the geolocation service which respects maximumAge
  // set in the DOM JS.

  DBG("geo: GPS got a fix (%f, %f). accuracy: %f",
      location->latitude,
      location->longitude,
      location->accuracy);

  RefPtr<UpdateLocationEvent> event = new UpdateLocationEvent(somewhere);
  NS_DispatchToMainThread(event);

#ifdef MOZ_B2G_RIL
  // Don't collect stumble data if lacking of API key of location server.
  if (gHasAuthorizationKey) {
    MozStumble(somewhere);
  }
#endif
}

class NotifyObserversGPSTask final : public nsRunnable
{
public:
  explicit NotifyObserversGPSTask(const char16_t* aData)
    : mData(aData)
  {}
  NS_IMETHOD Run() override {
    RefPtr<nsIGeolocationProvider> provider =
    GonkGPSGeolocationProvider::GetSingleton();
    nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
    obsService->NotifyObservers(provider, "geolocation-device-events", mData);
    return NS_OK;
  }
private:
  const char16_t* mData;
};

void
GonkGPSGeolocationProvider::StatusCallback(GpsStatus* status)
{
  const char* msgStream=0;
  switch (status->status) {
    case GPS_STATUS_NONE:
      msgStream = "geo: GPS_STATUS_NONE\n";
      break;
    case GPS_STATUS_SESSION_BEGIN:
      msgStream = "geo: GPS_STATUS_SESSION_BEGIN\n";
      break;
    case GPS_STATUS_SESSION_END:
      msgStream = "geo: GPS_STATUS_SESSION_END\n";
      break;
    case GPS_STATUS_ENGINE_ON:
      msgStream = "geo: GPS_STATUS_ENGINE_ON\n";
      NS_DispatchToMainThread(new NotifyObserversGPSTask( MOZ_UTF16("GPSStarting")));
      break;
    case GPS_STATUS_ENGINE_OFF:
      msgStream = "geo: GPS_STATUS_ENGINE_OFF\n";
      NS_DispatchToMainThread(new NotifyObserversGPSTask( MOZ_UTF16("GPSShutdown")));
      break;
    default:
      msgStream = "geo: Unknown GPS status\n";
      break;
  }
  DBG("%s", msgStream);
}

void
GonkGPSGeolocationProvider::SvStatusCallback(GpsSvStatus* sv_info)
{
  char enableSatellite[PROPERTY_VALUE_MAX + 1];
  property_get("mmitestgps.satellite.enabled", enableSatellite, "false");
  if (gDebug_isLoggingEnabled || !strcmp(enableSatellite, "true")) {
    static int numSvs = 0;
    static uint32_t numEphemeris = 0;
    static uint32_t numAlmanac = 0;
    static uint32_t numUsedInFix = 0;

    uint32_t i = 1;
    uint32_t svAlmanacCount = 0;
    uint32_t svEphemerisCount = 0;
    uint32_t svUsedCount = 0;
#if !defined(PRODUCT_MANUFACTURER_SPRD) || ANDROID_VERSION != 19
    for (i = 1; i > 0; i <<= 1) {
      if (i & sv_info->almanac_mask) {
        svAlmanacCount++;
      }
    }

    for (i = 1; i > 0; i <<= 1) {
      if (i & sv_info->ephemeris_mask) {
        svEphemerisCount++;
      }
    }

    for (i = 1; i > 0; i <<= 1) {
      if (i & sv_info->used_in_fix_mask) {
        svUsedCount++;
      }
    }
#else
    // KitKat HAL is modified by SPRD for supporting multi-gnss
    int multiGnss = sizeof(sv_info->almanac_mask) / sizeof(uint32_t);
    for (int k = 0; k < multiGnss; ++k) {
      for (i = 1; i > 0; i <<= 1) {
        if (i & sv_info->almanac_mask[k]) {
          svAlmanacCount++;
        }
      }

      for (i = 1; i > 0; i <<= 1) {
        if (i & sv_info->ephemeris_mask[k]) {
          svEphemerisCount++;
        }
      }

      for (i = 1; i > 0; i <<= 1) {
        if (i & sv_info->used_in_fix_mask[k]) {
          svUsedCount++;
        }
      }
    }
#endif
    // Log the message only if the the status changed.
    if (sv_info->num_svs != numSvs ||
        svAlmanacCount != numAlmanac ||
        svEphemerisCount != numEphemeris ||
        svUsedCount != numUsedInFix) {

      LOG(
        "geo: Number of SVs have (visibility, almanac, ephemeris): (%d, %d, %d)."
        "  %d of these SVs were used in fix.\n",
        sv_info->num_svs, svAlmanacCount, svEphemerisCount, svUsedCount);

      numSvs = sv_info->num_svs;
      numAlmanac = svAlmanacCount;
      numEphemeris = svEphemerisCount;
      numUsedInFix = svUsedCount;
    }

   //If there are other places set the enableSatellite is true,
   //it will generate a file ,the path and name is /data/mmitest_log/gps_info.txt.
    if (!strcmp(enableSatellite, "true")) {
      static FILE * fp = NULL;
      if (fp != NULL) {
        return;
      }
      if (sv_info->num_svs >= 0) {
        int num = sv_info->num_svs;
        mode_t procMask = umask(0);
        const char* path = "/data/mmitest_log";
        DIR *dp;
        if ((dp = opendir(path)) == NULL) {
          if (mkdir(path, 0755) < 0){
            LOG("can't create directory %s\n", path);
            return;
          }
        } else {
          closedir(dp);
        }
        fp = fopen("/data/mmitest_log/gps_info.txt", "wb");
        if (NULL == fp) {
          LOG("fp == NULL\n");
          return;
        }
        fprintf(fp, "{");

        fprintf(fp, "\"num\": %d", num);
#if !defined(PRODUCT_MANUFACTURER_SPRD) || ANDROID_VERSION != 19
        fprintf(fp, ", \"almanac_mask\": %d", sv_info->almanac_mask);
        fprintf(fp, ", \"ephemeris_mask\": %d", sv_info->ephemeris_mask);
        fprintf(fp, ", \"used_in_fix_mask\": %d", sv_info->used_in_fix_mask);
#else
        // only print the 1st mask since mmi-test app can't handle multi-gnss
        fprintf(fp, ", \"almanac_mask\": %d", sv_info->almanac_mask[0]);
        fprintf(fp, ", \"ephemeris_mask\": %d", sv_info->ephemeris_mask[0]);
        fprintf(fp, ", \"used_in_fix_mask\": %d", sv_info->used_in_fix_mask[0]);
#endif
        LOG("cck sv_info->num_svs = %d, num = %d\n",sv_info->num_svs, num);
        if (sv_info->num_svs != 0) {
          fprintf(fp, ",");
        }

        if (num > 0) {
          fprintf(fp, "\"gps\": [");

          for(int i = 0; i < num; i++) {
            fprintf(fp, "{\"prn\": %d, \"snr\": %f, \"elevation\": %f, \"azimuth\": %f}",
              sv_info->sv_list[i].prn, sv_info->sv_list[i].snr,
              sv_info->sv_list[i].elevation, sv_info->sv_list[i].azimuth);
            if (i < num - 1) {
              fprintf(fp, ",");
            }
          }

          fprintf(fp, "]");
        }
        fprintf(fp, "}");
        fclose(fp);
        umask(procMask);
        fp = NULL;
      }
    }
  }
}

#ifdef HAS_KOOST_MODULES
class NotifyNmeaUpdateTask final : public nsRunnable
{
public:
  NotifyNmeaUpdateTask(const int64_t aTimestamp, const char* aNmea)
    : mTimestamp(aTimestamp)
    , mNmea(aNmea)
  {}
  NS_IMETHOD Run() override {
    // Notify the child processes.
    nsTArray<ContentParent*> children;
    ContentParent::GetAll(children);
    for (uint32_t i = 0; i < children.Length(); i++) {
      if (children[i]->HasGeolocationWatcher()) {
        Unused << children[i]->SendGnssNmeaUpdate(mTimestamp, mNmea);
      }
    }
    return NS_OK;
  }
private:
  int64_t mTimestamp;
  nsCString mNmea;
};
#endif

void
GonkGPSGeolocationProvider::NmeaCallback(GpsUtcTime timestamp, const char* nmea, int length)
{
  DBG("NMEA: timestamp:\t%lld, length: %d, %s", timestamp, length, nmea);

#ifdef HAS_KOOST_MODULES
  NS_DispatchToMainThread(new NotifyNmeaUpdateTask(timestamp, nmea));
#endif

  char enableNMEA[PROPERTY_VALUE_MAX + 1];
  property_get("sgps.nmea.enabled", enableNMEA, "false");
  if (!strcmp(enableNMEA, "true")) {
    static FILE * fp = NULL;
    if (fp != NULL) {
      return;
    }
    const char* path = "/data/sgps_log";
    DIR *dp;
    if ((dp = opendir(path)) == NULL) {
      if (mkdir(path, 0755) < 0){
        LOG("can't create directory %s\n", path);
        return;
      }
    } else {
      closedir(dp);
    }
    fp = fopen("/data/sgps_log/Nmealog.txt", "a+");
    if (NULL == fp) {
      LOG("fp == NULL\n");
      return;
    }
    fprintf(fp, nmea);
    fclose(fp);
    fp = NULL;
  }
}

void
GonkGPSGeolocationProvider::SetCapabilitiesCallback(uint32_t capabilities)
{
  class UpdateCapabilitiesEvent : public nsRunnable {
  public:
    UpdateCapabilitiesEvent(uint32_t aCapabilities)
      : mCapabilities(aCapabilities)
    {}
    NS_IMETHOD Run() {
      RefPtr<GonkGPSGeolocationProvider> provider =
        GonkGPSGeolocationProvider::GetSingleton();

      provider->mSupportsScheduling = mCapabilities & GPS_CAPABILITY_SCHEDULING;
#ifdef MOZ_B2G_RIL
      provider->mSupportsMSB = mCapabilities & GPS_CAPABILITY_MSB;
      provider->mSupportsMSA = mCapabilities & GPS_CAPABILITY_MSA;
#endif
      provider->mSupportsSingleShot = mCapabilities & GPS_CAPABILITY_SINGLE_SHOT;
#ifdef GPS_CAPABILITY_ON_DEMAND_TIME
      provider->mSupportsTimeInjection = mCapabilities & GPS_CAPABILITY_ON_DEMAND_TIME;
#endif
      return NS_OK;
    }
  private:
    uint32_t mCapabilities;
  };

  NS_DispatchToMainThread(new UpdateCapabilitiesEvent(capabilities));
}

void
GonkGPSGeolocationProvider::AcquireWakelockCallback()
{
  acquire_wake_lock(PARTIAL_WAKE_LOCK, kWakeLockName);
}

void
GonkGPSGeolocationProvider::ReleaseWakelockCallback()
{
  release_wake_lock(kWakeLockName);
}

typedef void *(*pthread_func)(void *);

/** Callback for creating a thread that can call into the JS codes.
 */
pthread_t
GonkGPSGeolocationProvider::CreateThreadCallback(const char* name, void (*start)(void *), void* arg)
{
  pthread_t thread;
  pthread_attr_t attr;

  pthread_attr_init(&attr);

  /* Unfortunately pthread_create and the callback disagreed on what
   * start function should return.
   */
  pthread_create(&thread, &attr, reinterpret_cast<pthread_func>(start), arg);

  return thread;
}

void
GonkGPSGeolocationProvider::RequestUtcTimeCallback()
{
}

void
GonkGPSGeolocationProvider::SetNiResponse(int id, int response)
{
  LOG("GpsNiInterface->respond(%d, %d)", id, response);
  MOZ_ASSERT(mGpsNiInterface);
  mGpsNiInterface->respond(id, response);
}

bool
GonkGPSGeolocationProvider::SendChromeEvent(int id, GpsNiNotifyFlags flags, nsString& message)
{
  LOG("SendChromeEvent(%d, %x, %s)", id, flags, NS_ConvertUTF16toUTF8(message).get());
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    if (flags == GPS_NI_NEED_VERIFY) {
      RefPtr<GonkGPSGeolocationProvider> provider = GonkGPSGeolocationProvider::GetSingleton();
      provider->SetNiResponse(id, GPS_NI_RESPONSE_NORESP);
    }
    ERR("geo: SendChromeEvent Failed(id:%d, flags:%x)", id, flags);
    return false;
  }

  if (flags == GPS_NI_NEED_TIMEOUT) {
    obs->NotifyObservers(nullptr, SUPL_NI_VERIFY_TIMEOUT, message.get());
    return true;
  }

  bool needNotify = (flags & GPS_NI_NEED_NOTIFY) != 0;
  bool needVerify = (flags & GPS_NI_NEED_VERIFY) != 0;
  bool privacyOverride = (flags & GPS_NI_PRIVACY_OVERRIDE) != 0;


  // Verify behavior in gaia include notify and dialog in current
  // implementation.
  // So the logic here is different from Android
  if (needNotify && needVerify) {
    obs->NotifyObservers(nullptr, SUPL_NI_VERIFY, message.get());
  } else if (needNotify) {
    obs->NotifyObservers(nullptr, SUPL_NI_NOTIFY, message.get());
  } else {
    RefPtr<GonkGPSGeolocationProvider> provider =
      GonkGPSGeolocationProvider::GetSingleton();
    provider->SetNiResponse(id, GPS_NI_RESPONSE_ACCEPT);
  }

  if (privacyOverride) {
    RefPtr<GonkGPSGeolocationProvider> provider =
      GonkGPSGeolocationProvider::GetSingleton();
    provider->SetNiResponse(id, GPS_NI_RESPONSE_ACCEPT);
  }
  return true;
}

void
GonkGPSGeolocationProvider::GPSNiNotifyCallback(GpsNiNotification *notification)
{

  class GPSNiNotifyEvent : public nsRunnable {
  public:
    GPSNiNotifyEvent(GpsNiNotification *aNotification)
      : mNotification(aNotification)
    {}
    NS_IMETHOD Run() {
      int id = mNotification->notification_id;
      GpsNiNotifyFlags flags = mNotification->notify_flags;

      DBG("geo: GPSNiNotifyCallback id:%d, flag:%x, timeout:%d, default response:%d",
         id, flags, mNotification->timeout, mNotification->default_response);

      RefPtr<GonkGPSGeolocationProvider> provider =
        GonkGPSGeolocationProvider::GetSingleton();

      // The message is composed by "notification_id, notification_text,
      // notification_requestor_id"
      // If the encoding of text and requestor_id is not supported, the message
      // would be "id, ,".
      nsString message;
      message.AppendInt(id);
      message.AppendLiteral(",");
      GpsNiEncodingType encodingType = mNotification->text_encoding;
      if (encodingType == GPS_ENC_SUPL_UTF8 ||
          encodingType == GPS_ENC_SUPL_UCS2 ||
          encodingType == GPS_ENC_SUPL_GSM_DEFAULT) {
        message.Append(DecodeNIString(mNotification->text, encodingType));
      } else {
        LOG("geo: GPSNiNotifyCallback text in unsupport decodeing format(%d)", encodingType);
      }
      message.AppendLiteral(",");
      encodingType = mNotification->requestor_id_encoding;
      if (encodingType == GPS_ENC_SUPL_UTF8 ||
          encodingType == GPS_ENC_SUPL_UCS2 ||
          encodingType == GPS_ENC_SUPL_GSM_DEFAULT) {
        message.Append(DecodeNIString(mNotification->requestor_id, encodingType));
      } else {
        LOG("geo: GPSNiNotifyCallback requestor_id in unsupport decodeing format(%d)", encodingType);
      }
      provider->SendChromeEvent(id, flags, message);


      class TimeoutResponseEvent : public Task {
        public:
          TimeoutResponseEvent(int id, int defaultResp, nsString msg)
            : mId(id), mDefaultResp(defaultResp), mMsg(msg)
          {}
          void Run() {
            for (uint32_t idx = 0; idx < repliedSuplNiReqIds.Length() ; idx++) {
              if(repliedSuplNiReqIds[idx] == mId) {
                repliedSuplNiReqIds.RemoveElementAt(idx);
                return;
              }
            }
            RefPtr<GonkGPSGeolocationProvider> provider =
              GonkGPSGeolocationProvider::GetSingleton();
            if (!provider->SendChromeEvent(mId, GPS_NI_NEED_TIMEOUT, mMsg)) {
              ERR("geo: SendChromeEvent Failed(id:%d, flags:%x", mId, GPS_NI_NEED_TIMEOUT);
            }
            provider->SetNiResponse(mId, mDefaultResp);
            return;
          }
        private:
          int mId;
          int mDefaultResp;
          nsString mMsg;
      };

      bool needVerify = (flags & GPS_NI_NEED_VERIFY) != 0;
      if (needVerify) {
        MessageLoop::current()->PostDelayedTask(FROM_HERE,
          new TimeoutResponseEvent(id, mNotification->default_response, message),
          mNotification->timeout*1000);
      }
      return NS_OK;
    }
  private:
    GpsNiNotification *mNotification;
  };

  NS_DispatchToMainThread(new GPSNiNotifyEvent(notification));
  return;
}

#ifdef MOZ_B2G_RIL
void
GonkGPSGeolocationProvider::AGPSStatusCallback(AGpsStatus* status)
{
  MOZ_ASSERT(status);

  class AGPSStatusEvent : public nsRunnable {
  public:
    AGPSStatusEvent(AGpsStatusValue aStatus)
      : mStatus(aStatus)
    {}
    NS_IMETHOD Run() {
      RefPtr<GonkGPSGeolocationProvider> provider =
        GonkGPSGeolocationProvider::GetSingleton();

      switch (mStatus) {
        case GPS_REQUEST_AGPS_DATA_CONN:
          provider->RequestDataConnection();
          break;
        case GPS_RELEASE_AGPS_DATA_CONN:
          provider->ReleaseDataConnection();
          break;
      }
      return NS_OK;
    }
  private:
    AGpsStatusValue mStatus;
  };

  NS_DispatchToMainThread(new AGPSStatusEvent(status->status));
}

void
GonkGPSGeolocationProvider::AGPSRILSetIDCallback(uint32_t flags)
{
  class RequestSetIDEvent : public nsRunnable {
  public:
    RequestSetIDEvent(uint32_t flags)
      : mFlags(flags)
    {}
    NS_IMETHOD Run() {
      RefPtr<GonkGPSGeolocationProvider> provider =
        GonkGPSGeolocationProvider::GetSingleton();
      provider->RequestSetID(mFlags);
      return NS_OK;
    }
  private:
    uint32_t mFlags;
  };

  NS_DispatchToMainThread(new RequestSetIDEvent(flags));
}

void
GonkGPSGeolocationProvider::AGPSRILRefLocCallback(uint32_t flags)
{
  class RequestRefLocEvent : public nsRunnable {
  public:
    RequestRefLocEvent()
    {}
    NS_IMETHOD Run() {
      RefPtr<GonkGPSGeolocationProvider> provider =
        GonkGPSGeolocationProvider::GetSingleton();
      provider->SetReferenceLocation();
      return NS_OK;
    }
  };

  // For fault tolerance, provide ref location even flags is set to 0
  if ((flags & AGPS_RIL_REQUEST_REFLOC_CELLID) || flags == 0) {
    NS_DispatchToMainThread(new RequestRefLocEvent());
  }
}
#endif // MOZ_B2G_RIL

GonkGPSGeolocationProvider::GonkGPSGeolocationProvider()
  : mInitialized(false)
  , mStarted(false)
  , mSupportsScheduling(false)
#ifdef MOZ_B2G_RIL
  , mSupportsMSB(false)
  , mSupportsMSA(false)
  , mRilDataServiceId(0)
  , mNumberOfRilServices(1)
  , mObservingNetworkConnStateChange(false)
#endif
  , mObservingSettingsChange(false)
  , mSupportsSingleShot(false)
  , mSupportsTimeInjection(false)
  , mGpsMode(0)
  , mGpsInterface(nullptr)
{
}

GonkGPSGeolocationProvider::~GonkGPSGeolocationProvider()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mStarted, "Must call Shutdown before destruction");
  if (mGpsInterface) {
    mGpsInterface->cleanup();
  }

  if (mObservingSettingsChange) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      nsresult rv;
      rv = obs->RemoveObserver(this, kMozSettingsChangedTopic);
      if (NS_FAILED(rv)) {
        NS_WARNING("geo: Gonk GPS mozsettings RemoveObserver failed");
      } else {
        mObservingSettingsChange = false;
      }
    }
  }

  sSingleton = nullptr;
}

already_AddRefed<GonkGPSGeolocationProvider>
GonkGPSGeolocationProvider::GetSingleton()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!sSingleton)
    sSingleton = new GonkGPSGeolocationProvider();

  RefPtr<GonkGPSGeolocationProvider> provider = sSingleton;
  return provider.forget();
}

const GpsInterface*
GonkGPSGeolocationProvider::GetGPSInterface()
{
  hw_module_t* module;

  if (hw_get_module(GPS_HARDWARE_MODULE_ID, (hw_module_t const**)&module))
    return nullptr;

  hw_device_t* device;
  if (module->methods->open(module, GPS_HARDWARE_MODULE_ID, &device))
    return nullptr;

  gps_device_t* gps_device = (gps_device_t *)device;
  const GpsInterface* result = gps_device->get_gps_interface(gps_device);

  if (!result || result->size != sizeof(GpsInterface)) {
    ERR("GpsInterface is not available.");
    return nullptr;
  }
  return result;
}

#ifdef MOZ_B2G_RIL
int32_t
GonkGPSGeolocationProvider::GetDataConnectionState()
{
  if (!mRadioInterface) {
    return nsINetworkInfo::NETWORK_STATE_UNKNOWN;
  }

  int32_t state;
  mRadioInterface->GetDataCallStateByType(
    nsINetworkInfo::NETWORK_TYPE_MOBILE_SUPL, &state);
  return state;
}

void
GonkGPSGeolocationProvider::SetAGpsDataConn(nsAString& aApn)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mAGpsInterface);

  bool hasUpdateNetworkAvailability = false;
  if (mAGpsRilInterface &&
      mAGpsRilInterface->size >= sizeof(AGpsRilInterface) &&
      mAGpsRilInterface->update_network_availability) {
    hasUpdateNetworkAvailability = true;
  }

  int32_t connectionState = GetDataConnectionState();
  NS_ConvertUTF16toUTF8 apn(aApn);
  if (connectionState == nsINetworkInfo::NETWORK_STATE_CONNECTED) {
    // The definition of availability is
    // 1. The device is connected to the home network
    // 2. The device is connected to a foreign network and data
    //    roaming is enabled
    // RIL turns on/off data connection automatically when the data
    // roaming setting changes.
    if (hasUpdateNetworkAvailability) {
      mAGpsRilInterface->update_network_availability(true, apn.get());
    }
// data_conn_open has been deprecated since Android L added ApnIpType to HAL
// Check APN_IP_IPV4V6 to see whether data_conn_open_with_apn_ip_type is supported.
#ifdef APN_IP_IPV4V6
    LOG("AGpsInterface->data_conn_open_with_apn_ip_type(%s, APN_IP_IPV4V6)", apn.get());
    mAGpsInterface->data_conn_open_with_apn_ip_type(apn.get(), APN_IP_IPV4V6);
#else
    LOG("AGpsInterface->data_conn_open(%s)", apn.get());
    mAGpsInterface->data_conn_open(apn.get());
#endif
  } else if (connectionState == nsINetworkInfo::NETWORK_STATE_DISCONNECTED) {
    if (hasUpdateNetworkAvailability) {
      mAGpsRilInterface->update_network_availability(false, apn.get());
    }
    LOG("AGpsInterface->data_conn_closed()");
    mAGpsInterface->data_conn_closed();
  }
}

#endif // MOZ_B2G_RIL

void
GonkGPSGeolocationProvider::RequestSettingValue(const char* aKey)
{
  MOZ_ASSERT(aKey);
  nsCOMPtr<nsISettingsService> ss = do_GetService("@mozilla.org/settingsService;1");
  if (!ss) {
    MOZ_ASSERT(ss);
    return;
  }

  nsCOMPtr<nsISettingsServiceLock> lock;
  nsresult rv = ss->CreateLock(nullptr, getter_AddRefs(lock));
  if (NS_FAILED(rv)) {
    ERR("error while createLock setting '%s': %d\n", aKey, uint32_t(rv));
    return;
  }

  rv = lock->Get(aKey, this);
  if (NS_FAILED(rv)) {
    ERR("error while get setting '%s': %d\n", aKey, uint32_t(rv));
    return;
  }
}

#ifdef MOZ_B2G_RIL
void
GonkGPSGeolocationProvider::RequestDataConnection()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mRadioInterface) {
    return;
  }

  if (GetDataConnectionState() == nsINetworkInfo::NETWORK_STATE_CONNECTED) {
    // Connection is already established, we don't need to setup again.
    // We just get supl APN and make AGPS data connection state updated.
    DBG("Request setting 'ril.supl.apn' since SUPL is already connected.");
    RequestSettingValue("ril.supl.apn");
  } else {

    if (!mObservingNetworkConnStateChange) {
      LOG("Add network state changed observer before updating mRilDataServiceId");

      nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
      if (!obs || NS_FAILED(obs->AddObserver(this, kNetworkConnStateChangedTopic, false))) {
        NS_WARNING("Failed to add network state changed observer!");
      } else {
        mObservingNetworkConnStateChange = true;
      }
    }

    DBG("nsIRadioInterface->SetupDataCallByType()");
    mRadioInterface->SetupDataCallByType(nsINetworkInfo::NETWORK_TYPE_MOBILE_SUPL);
  }
}

void
GonkGPSGeolocationProvider::ReleaseDataConnection()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mRadioInterface) {
    return;
  }

  LOG("nsIRadioInterface->DeactivateDataCallByType()");
  mRadioInterface->DeactivateDataCallByType(nsINetworkInfo::NETWORK_TYPE_MOBILE_SUPL);
}

void
GonkGPSGeolocationProvider::RequestSetID(uint32_t flags)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mRadioInterface ||
      !mAGpsInterface) {
    return;
  }

  AGpsSetIDType type = AGPS_SETID_TYPE_NONE;

  nsCOMPtr<nsIIccService> iccService =
    do_GetService(ICC_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE_VOID(iccService);

  nsCOMPtr<nsIIcc> icc;
  iccService->GetIccByServiceId(mRilDataServiceId, getter_AddRefs(icc));
  NS_ENSURE_TRUE_VOID(icc);

  nsAutoString id;

  if (flags & AGPS_RIL_REQUEST_SETID_IMSI) {
    type = AGPS_SETID_TYPE_IMSI;
    icc->GetImsi(id);
  }

  if (flags & AGPS_RIL_REQUEST_SETID_MSISDN) {
    nsCOMPtr<nsIIccInfo> iccInfo;
    icc->GetIccInfo(getter_AddRefs(iccInfo));
    if (iccInfo) {
      nsCOMPtr<nsIGsmIccInfo> gsmIccInfo = do_QueryInterface(iccInfo);
      if (gsmIccInfo) {
        type = AGPS_SETID_TYPE_MSISDN;
        gsmIccInfo->GetMsisdn(id);
      }
    }
  }

  NS_ConvertUTF16toUTF8 idBytes(id);
  DBG("AGpsRilInterface->set_set_id(%u, %s)", type, idBytes.get());
  mAGpsRilInterface->set_set_id(type, idBytes.get());
}

namespace {
int
ConvertToGpsRefLocationType(const nsAString& aConnectionType)
{
  const char* GSM_TYPES[] = { "gsm", "gprs", "edge" };
  const char* UMTS_TYPES[] = { "umts", "hspda", "hsupa", "hspa", "hspa+" };

  for (auto type: GSM_TYPES) {
    if (aConnectionType.EqualsASCII(type)) {
        return AGPS_REF_LOCATION_TYPE_GSM_CELLID;
    }
  }

  for (auto type: UMTS_TYPES) {
    if (aConnectionType.EqualsASCII(type)) {
      return AGPS_REF_LOCATION_TYPE_UMTS_CELLID;
    }
  }

  DBG("geo: Unsupported connection type %s\n",
      NS_ConvertUTF16toUTF8(aConnectionType).get());
  return AGPS_REF_LOCATION_TYPE_GSM_CELLID;
}
} // namespace

void
GonkGPSGeolocationProvider::SetReferenceLocation()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mRadioInterface ||
      !mAGpsRilInterface) {
    return;
  }

  AGpsRefLocation location;

  nsCOMPtr<nsIMobileConnectionService> service =
    do_GetService(NS_MOBILE_CONNECTION_SERVICE_CONTRACTID);
  if (!service) {
    NS_WARNING("Cannot get MobileConnectionService");
    return;
  }

  nsCOMPtr<nsIMobileConnection> connection;
  service->GetItemByServiceId(mRilDataServiceId, getter_AddRefs(connection));
  NS_ENSURE_TRUE_VOID(connection);

  nsCOMPtr<nsIMobileConnectionInfo> voice;
  connection->GetVoice(getter_AddRefs(voice));
  if (voice) {
    nsAutoString connectionType;
    nsresult rv = voice->GetType(connectionType);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }
    location.type = ConvertToGpsRefLocationType(connectionType);

    nsCOMPtr<nsIMobileNetworkInfo> networkInfo;
    voice->GetNetwork(getter_AddRefs(networkInfo));
    if (networkInfo) {
      nsresult result;
      nsAutoString mcc, mnc;

      networkInfo->GetMcc(mcc);
      networkInfo->GetMnc(mnc);

      location.u.cellID.mcc = mcc.ToInteger(&result);
      if (result != NS_OK) {
        NS_WARNING("Cannot parse mcc to integer");
        location.u.cellID.mcc = 0;
      }

      location.u.cellID.mnc = mnc.ToInteger(&result);
      if (result != NS_OK) {
        NS_WARNING("Cannot parse mnc to integer");
        location.u.cellID.mnc = 0;
      }
    } else {
      NS_WARNING("Cannot get mobile network info.");
      location.u.cellID.mcc = 0;
      location.u.cellID.mnc = 0;
    }

    nsCOMPtr<nsIMobileCellInfo> cell;
    voice->GetCell(getter_AddRefs(cell));
    if (cell) {
      int32_t lac;
      int64_t cid;

      cell->GetGsmLocationAreaCode(&lac);
      // The valid range of LAC is 0x0 to 0xffff which is defined in
      // hardware/ril/include/telephony/ril.h
      if (lac >= 0x0 && lac <= 0xffff) {
        location.u.cellID.lac = lac;
      }

      cell->GetGsmCellId(&cid);
      // The valid range of cell id is 0x0 to 0xffffffff which is defined in
      // hardware/ril/include/telephony/ril.h
      if (cid >= 0x0 && cid <= 0xffffffff) {
        location.u.cellID.cid = cid;
      }
    } else {
      NS_WARNING("Cannot get mobile cell info.");
      location.u.cellID.lac = -1;
      location.u.cellID.cid = -1;
    }
  } else {
    NS_WARNING("Cannot get mobile connection info.");
    return;
  }
  DBG("AGpsRilInterface->set_ref_location()");
  mAGpsRilInterface->set_ref_location(&location, sizeof(location));
}

#endif // MOZ_B2G_RIL

void
GonkGPSGeolocationProvider::InjectLocation(double latitude,
                                           double longitude,
                                           float accuracy)
{
  DBG("injecting location (%f, %f) accuracy: %f", latitude, longitude, accuracy);

  MOZ_ASSERT(NS_IsMainThread());
  if (!mGpsInterface) {
    return;
  }

  mGpsInterface->inject_location(latitude, longitude, accuracy);
}

void
GonkGPSGeolocationProvider::Init()
{
  // Must not be main thread. Some GPS driver's first init takes very long.
  MOZ_ASSERT(!NS_IsMainThread());

  mGpsInterface = GetGPSInterface();
  if (!mGpsInterface) {
    ERR("Failed to get GPS HAL.");
    return;
  }

  if (!mCallbacks.size) {
    mCallbacks.size = sizeof(GpsCallbacks);
    mCallbacks.location_cb = LocationCallback;
    mCallbacks.status_cb = StatusCallback;
    mCallbacks.sv_status_cb = SvStatusCallback;
    mCallbacks.nmea_cb = NmeaCallback;
    mCallbacks.set_capabilities_cb = SetCapabilitiesCallback;
    mCallbacks.acquire_wakelock_cb = AcquireWakelockCallback;
    mCallbacks.release_wakelock_cb = ReleaseWakelockCallback;
    mCallbacks.create_thread_cb = CreateThreadCallback;

#ifdef GPS_CAPABILITY_ON_DEMAND_TIME
    mCallbacks.request_utc_time_cb = RequestUtcTimeCallback;
#endif

    mGPSNiCallbacks.notify_cb = GPSNiNotifyCallback;
    mGPSNiCallbacks.create_thread_cb = CreateThreadCallback;

#ifdef MOZ_B2G_RIL
    mAGPSCallbacks.status_cb = AGPSStatusCallback;
    mAGPSCallbacks.create_thread_cb = CreateThreadCallback;

    mAGPSRILCallbacks.request_setid = AGPSRILSetIDCallback;
    mAGPSRILCallbacks.request_refloc = AGPSRILRefLocCallback;
    mAGPSRILCallbacks.create_thread_cb = CreateThreadCallback;
#endif
  }

  if (mGpsInterface->init(&mCallbacks) != 0) {
    ERR("Failed to init GPS HAL.");
    return;
  }

  mGpsNiInterface =
    static_cast<const GpsNiInterface*>(mGpsInterface->get_extension(GPS_NI_INTERFACE));
  if (mGpsNiInterface) {
    mGpsNiInterface->init(&mGPSNiCallbacks);
  }

#ifdef MOZ_B2G_RIL
  mAGpsInterface =
    static_cast<const AGpsInterface*>(mGpsInterface->get_extension(AGPS_INTERFACE));
  if (mAGpsInterface) {
    mAGpsInterface->init(&mAGPSCallbacks);
  }

  mAGpsRilInterface =
    static_cast<const AGpsRilInterface*>(mGpsInterface->get_extension(AGPS_RIL_INTERFACE));
  if (mAGpsRilInterface) {
    mAGpsRilInterface->init(&mAGPSRILCallbacks);
  }
#endif

  mInitialized = true;

  LOG("GPS HAL has been initialized");

  // If there is an ongoing location request, starts GPS navigating
  if (mStarted) {
    NS_DispatchToMainThread(NS_NewRunnableMethod(this, &GonkGPSGeolocationProvider::StartGPS));
  }
}

void
GonkGPSGeolocationProvider::StartGPS()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mGpsInterface);

  int32_t update = Preferences::GetInt("geo.default.update", kDefaultPeriod);

#ifdef MOZ_B2G_RIL
  if (mSupportsMSA || mSupportsMSB) {
    SetupAGPS();
  }
#endif

  int positionMode = GPS_POSITION_MODE_STANDALONE;

#ifdef MOZ_B2G_RIL
  bool singleShot = false;

  // XXX: If we know this is a single shot request, use MSA can be faster.
  if (singleShot && mSupportsMSA) {
    positionMode = GPS_POSITION_MODE_MS_ASSISTED;
  } else if (mSupportsMSB) {
    positionMode = GPS_POSITION_MODE_MS_BASED;
  }
#endif
  if (!mSupportsScheduling) {
    update = kDefaultPeriod;
  }

  mGpsInterface->set_position_mode(positionMode,
                                   GPS_POSITION_RECURRENCE_PERIODIC,
                                   update, 0, 0);
#if FLUSH_AIDE_DATA
  // Delete cached data
  mGpsInterface->delete_aiding_data(GPS_DELETE_ALL);
#endif

  DeleteGpsData(mGpsMode);
  LOG("Starts GPS");
  mGpsInterface->start();
}

#ifdef MOZ_B2G_RIL
void
GonkGPSGeolocationProvider::SetupAGPS()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mAGpsInterface);

  // Request RIL date service ID for correct RadioInterface object first due to
  // multi-SIM case needs it to handle AGPS related stuffs. For single SIM, 0
  // will be returned as default RIL data service ID.
  RequestSettingValue(kSettingRilDefaultServiceId);

  const nsAdoptingCString& suplServer = Preferences::GetCString("geo.gps.supl_server");
  int32_t suplPort = Preferences::GetInt("geo.gps.supl_port", -1);
  if (!suplServer.IsEmpty() && suplPort > 0) {
    DBG("AGpsInterface->set_server(%s, %d)", suplServer.get(), suplPort);
    mAGpsInterface->set_server(AGPS_TYPE_SUPL, suplServer.get(), suplPort);
  } else {
    NS_WARNING("Cannot get SUPL server settings");
    return;
  }
}

void
GonkGPSGeolocationProvider::UpdateRadioInterface()
{
  nsCOMPtr<nsIRadioInterfaceLayer> ril = do_GetService("@mozilla.org/ril;1");
  NS_ENSURE_TRUE_VOID(ril);
  ril->GetRadioInterface(mRilDataServiceId, getter_AddRefs(mRadioInterface));
}

bool
GonkGPSGeolocationProvider::IsValidRilServiceId(uint32_t aServiceId)
{
  return aServiceId < mNumberOfRilServices;
}
#endif // MOZ_B2G_RIL


NS_IMPL_ISUPPORTS(GonkGPSGeolocationProvider::NetworkLocationUpdate,
                  nsIGeolocationUpdate)

NS_IMETHODIMP
GonkGPSGeolocationProvider::NetworkLocationUpdate::Update(nsIDOMGeoPosition *position)
{
  RefPtr<GonkGPSGeolocationProvider> provider =
    GonkGPSGeolocationProvider::GetSingleton();

  nsCOMPtr<nsIDOMGeoPositionCoords> coords;
  position->GetCoords(getter_AddRefs(coords));
  if (!coords) {
    return NS_ERROR_FAILURE;
  }

  double lat, lon, acc;
  coords->GetLatitude(&lat);
  coords->GetLongitude(&lon);
  coords->GetAccuracy(&acc);

  double delta = -1.0;

  static double sLastMLSPosLat = 0.0;
  static double sLastMLSPosLon = 0.0;

  if (0 != sLastMLSPosLon || 0 != sLastMLSPosLat) {
    delta = CalculateDeltaInMeter(lat, lon, sLastMLSPosLat, sLastMLSPosLon);
  }

  sLastMLSPosLat = lat;
  sLastMLSPosLon = lon;

  // if the MLS coord change is smaller than this arbitrarily small value
  // assume the MLS coord is unchanged, and stick with the GPS location
  const double kMinMLSCoordChangeInMeters = 10.0;

  DOMTimeStamp time_ms = 0;
  if (provider->mLastGPSPosition) {
    provider->mLastGPSPosition->GetTimestamp(&time_ms);
  }
  const int64_t diff_ms = (PR_Now() / PR_USEC_PER_MSEC) - time_ms;

  // We want to distinguish between the GPS being inactive completely
  // and temporarily inactive. In the former case, we would use a low
  // accuracy network location; in the latter, we only want a network
  // location that appears to updating with movement.

  const bool isGPSFullyInactive = diff_ms > 1000 * 60 * 2; // two mins
  const bool isGPSTempInactive = diff_ms > 1000 * 10; // 10 secs

  if (provider->mLocationCallback) {
    if (isGPSFullyInactive ||
       (isGPSTempInactive && delta > kMinMLSCoordChangeInMeters))
    {
      DBG("Using MLS, GPS age:%fs, MLS Delta:%fm", diff_ms / 1000.0, delta);
      provider->mLocationCallback->Update(position);
    } else if (provider->mLastGPSPosition) {
      DBG("Using old GPS age:%fs", diff_ms / 1000.0);

      // This is a fallback case so that the GPS provider responds with its last
      // location rather than waiting for a more recent GPS or network location.
      // The service decides if the location is too old, not the provider.
      provider->mLocationCallback->Update(provider->mLastGPSPosition);
    }
  }
  provider->InjectLocation(lat, lon, acc);
  return NS_OK;
}
NS_IMETHODIMP
GonkGPSGeolocationProvider::NetworkLocationUpdate::NotifyError(uint16_t error)
{
  return NS_OK;
}
NS_IMETHODIMP
GonkGPSGeolocationProvider::Startup()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mStarted) {
    return NS_OK;
  }

  RequestSettingValue(kSettingDebugEnabled);
  RequestSettingValue(kSettingDebugGpsIgnored);

  // Setup an observer to watch changes to the setting.
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    MOZ_ASSERT(!mObservingSettingsChange);
    nsresult rv = observerService->AddObserver(this, kMozSettingsChangedTopic, false);
    if (NS_FAILED(rv)) {
      NS_WARNING("geo: Gonk GPS AddObserver failed");
    } else {
      mObservingSettingsChange = true;
    }
  }

  // Setup NetworkLocationProvider if the API key and server URI are available
  const nsAdoptingString& serverUri = Preferences::GetString("geo.wifi.uri");
  if (!serverUri.IsEmpty()) {
    nsresult rv;
    nsCOMPtr<nsIURLFormatter> formatter =
      do_CreateInstance("@mozilla.org/toolkit/URLFormatterService;1", &rv);
    if (NS_SUCCEEDED(rv)) {
      nsString key;
      rv = formatter->FormatURLPref(NS_LITERAL_STRING("geo.authorization.key"), key);
      if (NS_SUCCEEDED(rv) && !key.IsEmpty()) {
        gHasAuthorizationKey = true;
        mNetworkLocationProvider = do_CreateInstance("@mozilla.org/geolocation/mls-provider;1");
        if (mNetworkLocationProvider) {
          rv = mNetworkLocationProvider->Startup();
          if (NS_SUCCEEDED(rv)) {
            RefPtr<NetworkLocationUpdate> update = new NetworkLocationUpdate();
            mNetworkLocationProvider->Watch(update);
          }
        }
      }
    }
  }

  if (mInitialized) {
    NS_DispatchToMainThread(NS_NewRunnableMethod(this, &GonkGPSGeolocationProvider::StartGPS));
  } else {
    // Initialize GPS HAL first and it would start GPS after then.
    if (!mInitThread) {
      nsresult rv = NS_NewThread(getter_AddRefs(mInitThread));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    mInitThread->Dispatch(NS_NewRunnableMethod(this, &GonkGPSGeolocationProvider::Init),
                          NS_DISPATCH_NORMAL);
  }

  mStarted = true;
#ifdef MOZ_B2G_RIL
  mNumberOfRilServices = Preferences::GetUint(kPrefRilNumRadioInterfaces, 1);
#endif
  return NS_OK;
}

NS_IMETHODIMP
GonkGPSGeolocationProvider::Watch(nsIGeolocationUpdate* aCallback)
{
  MOZ_ASSERT(NS_IsMainThread());

  mLocationCallback = aCallback;
  return NS_OK;
}

NS_IMETHODIMP
GonkGPSGeolocationProvider::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mStarted) {
    return NS_OK;
  }

  mGpsMode = 0;
  mStarted = false;
  if (mNetworkLocationProvider) {
    mNetworkLocationProvider->Shutdown();
    mNetworkLocationProvider = nullptr;
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
#ifdef MOZ_B2G_RIL
    nsresult rv = obs->RemoveObserver(this, kNetworkConnStateChangedTopic);
    if (NS_FAILED(rv)) {
      NS_WARNING("geo: Gonk GPS network state RemoveObserver failed");
    } else {
      mObservingNetworkConnStateChange = false;
    }
#endif
  }

  mInitThread->Dispatch(NS_NewRunnableMethod(this, &GonkGPSGeolocationProvider::ShutdownGPS),
                        NS_DISPATCH_NORMAL);

  return NS_OK;
}

void
GonkGPSGeolocationProvider::ShutdownGPS()
{
  MOZ_ASSERT(!mStarted, "Should only be called after Shutdown");

  if (mGpsInterface) {
    LOG("Stops GPS");
    mGpsInterface->stop();
  }
}

//Check if data needs to be deleted
NS_IMETHODIMP
GonkGPSGeolocationProvider::DeleteGpsData(uint16_t gpsMode)
{
  switch (gpsMode) {
    case GPS_DELETE_ALL:
      mGpsMode = GPS_DELETE_ALL;
      break;
    case GPS_DELETE_EPHEMERIS:
      mGpsMode = GPS_DELETE_EPHEMERIS;
      break;
    default:
      LOG("Received unsupported GPS data type 0x%x for deleting", gpsMode);
      mGpsMode = 0;
      break;
  }

  if (mGpsInterface && mInitialized && mGpsMode) {
    LOG("Delete GPS aiding data with gps mode 0x%x", mGpsMode);
    mGpsInterface->delete_aiding_data(mGpsMode);
    mGpsMode = 0;
  }

  return NS_OK;
}

 NS_IMETHODIMP
GonkGPSGeolocationProvider::SetGpsDeleteType(uint16_t gpsMode)
{
  mGpsMode = gpsMode;
  return NS_OK;
}

NS_IMETHODIMP
GonkGPSGeolocationProvider::SetHighAccuracy(bool)
{
  return NS_OK;
}

#ifdef MOZ_B2G_RIL
namespace {
int
ConvertToGpsNetworkType(int aNetworkInterfaceType)
{
  switch (aNetworkInterfaceType) {
    case nsINetworkInfo::NETWORK_TYPE_WIFI:
      return AGPS_RIL_NETWORK_TYPE_WIFI;
    case nsINetworkInfo::NETWORK_TYPE_MOBILE:
      return AGPS_RIL_NETWORK_TYPE_MOBILE;
    case nsINetworkInfo::NETWORK_TYPE_MOBILE_MMS:
      return AGPS_RIL_NETWORK_TYPE_MOBILE_MMS;
    case nsINetworkInfo::NETWORK_TYPE_MOBILE_SUPL:
      return AGPS_RIL_NETWORK_TYPE_MOBILE_SUPL;
    case nsINetworkInfo::NETWORK_TYPE_MOBILE_DUN:
      return AGPS_RIL_NETWORK_TTYPE_MOBILE_DUN;
    default:
      NS_WARNING(nsPrintfCString("Unknown network type mapping %d",
                                 aNetworkInterfaceType).get());
      return -1;
  }
}
} // namespace
#endif

NS_IMETHODIMP
GonkGPSGeolocationProvider::Observe(nsISupports* aSubject,
                                    const char* aTopic,
                                    const char16_t* aData)
{
  MOZ_ASSERT(NS_IsMainThread());

#ifdef MOZ_B2G_RIL
  if (!strcmp(aTopic, kNetworkConnStateChangedTopic)) {
    nsCOMPtr<nsINetworkInfo> info = do_QueryInterface(aSubject);
    if (!info) {
      return NS_OK;
    }
    nsCOMPtr<nsIRilNetworkInfo> rilInfo = do_QueryInterface(aSubject);
    if (mAGpsRilInterface && mAGpsRilInterface->update_network_state) {
      int32_t state;
      int32_t type;
      info->GetState(&state);
      info->GetType(&type);
      bool connected = (state == nsINetworkInfo::NETWORK_STATE_CONNECTED);
      bool roaming = false;
      int gpsNetworkType = ConvertToGpsNetworkType(type);
      if (gpsNetworkType >= 0) {
        if (rilInfo) {
          do {
            nsCOMPtr<nsIMobileConnectionService> service =
              do_GetService(NS_MOBILE_CONNECTION_SERVICE_CONTRACTID);
            if (!service) {
              break;
            }

            nsCOMPtr<nsIMobileConnection> connection;
            service->GetItemByServiceId(mRilDataServiceId, getter_AddRefs(connection));
            if (!connection) {
              break;
            }

            nsCOMPtr<nsIMobileConnectionInfo> voice;
            connection->GetVoice(getter_AddRefs(voice));
            if (voice) {
              voice->GetRoaming(&roaming);
            }
          } while (0);
        }
        DBG("AGpsRilInterface->update_network_state(%d, %d, %d)", connected,
          gpsNetworkType, roaming);
        mAGpsRilInterface->update_network_state(
          connected,
          gpsNetworkType,
          roaming,
          /* extra_info = */ nullptr);
      }
    }
    // No data connection
    if (!rilInfo) {
      return NS_OK;
    }

    RequestSettingValue("ril.supl.apn");
  }
#endif

  if (!strcmp(aTopic, kMozSettingsChangedTopic)) {
    // Read changed setting value
    RootedDictionary<SettingChangeNotification> setting(nsContentUtils::RootingCx());
    if (!WrappedJSToDictionary(aSubject, setting)) {
      return NS_OK;
    }

    if (setting.mKey.EqualsASCII(kSettingDebugGpsIgnored)) {
      gDebug_isGPSLocationIgnored =
        setting.mValue.isBoolean() ? setting.mValue.toBoolean() : false;
      LOG("received mozsettings-changed: ignoring: %d", gDebug_isGPSLocationIgnored);
      return NS_OK;
    } else if (setting.mKey.EqualsASCII(kSettingDebugEnabled)) {
      gDebug_isLoggingEnabled =
        setting.mValue.isBoolean() ? setting.mValue.toBoolean() : false;
      LOG("received mozsettings-changed: logging: %d", gDebug_isLoggingEnabled);
      return NS_OK;
    } else if (setting.mKey.EqualsASCII(kSettingGeolocationEnabled)) {
      bool isGeolocationEnabled =
        setting.mValue.isBoolean() ? setting.mValue.toBoolean() : false;
      LOG("received mozsettings-changed: geolocation-enabled: %d", isGeolocationEnabled);

      if (!isGeolocationEnabled) {
        if (mInitialized && mGpsInterface &&
            Preferences::GetBool(kPrefOndemandCleanup)) {
          // Cleanup GPS HAL when Geolocation setting is turned off
          mGpsInterface->stop();
          mGpsInterface->cleanup();
          mInitialized = false;
        }

        nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
        if (obs) {
          nsresult rv;
          rv = obs->RemoveObserver(this, kMozSettingsChangedTopic);
          if (NS_FAILED(rv)) {
            NS_WARNING("geo: Gonk GPS mozsettings RemoveObserver failed");
          } else {
            mObservingSettingsChange = false;
          }
        }
      }

      return NS_OK;
    } else if (setting.mKey.EqualsASCII(kSettingSuplVerificationChoice)) {
      LOG("geo: received the choice of supl ni verification");
      int id = setting.mValue.toNumber();
      RefPtr<GonkGPSGeolocationProvider> provider =
        GonkGPSGeolocationProvider::GetSingleton();
      // The value of this key is based on notification_id:
      //   positive value(notification_id) means the choice is yes,
      //   negative value(notification_id * -1) means the choice is no.
      if (id >= 0) {
        provider->SetNiResponse(id, GPS_NI_RESPONSE_ACCEPT);
        repliedSuplNiReqIds.AppendElement(id);
      } else {
        provider->SetNiResponse(id*-1, GPS_NI_RESPONSE_DENY);
        repliedSuplNiReqIds.AppendElement(id*-1);
      }
      return NS_OK;
    }
#ifdef MOZ_B2G_RIL
    else if (setting.mKey.EqualsASCII(kSettingRilDefaultServiceId)) {
      if (!setting.mValue.isNumber() ||
          !IsValidRilServiceId(setting.mValue.toNumber())) {
        return NS_ERROR_UNEXPECTED;
      }

      mRilDataServiceId = setting.mValue.toNumber();
      UpdateRadioInterface();
      return NS_OK;
    }
#endif
  }

  return NS_OK;
}

/** nsISettingsServiceCallback **/

NS_IMETHODIMP
GonkGPSGeolocationProvider::Handle(const nsAString& aName,
                                   JS::Handle<JS::Value> aResult)
{
#ifdef MOZ_B2G_RIL
  if (aName.EqualsLiteral("ril.supl.apn")) {
    // When we get the APN, we attempt to call data_call_open of AGPS.
    if (aResult.isString()) {
      JSContext *cx = nsContentUtils::GetCurrentJSContext();
      NS_ENSURE_TRUE(cx, NS_OK);

      // NB: No need to enter a compartment to read the contents of a string.
      nsAutoJSString apn;
      if (!apn.init(cx, aResult.toString())) {
        return NS_ERROR_FAILURE;
      }
      if (!apn.IsEmpty()) {
        SetAGpsDataConn(apn);
      }
    }
  } else if (aName.EqualsASCII(kSettingRilDefaultServiceId)) {
    uint32_t id = 0;
    JSContext *cx = nsContentUtils::GetCurrentJSContext();
    NS_ENSURE_TRUE(cx, NS_OK);
    if (!JS::ToUint32(cx, aResult, &id)) {
      return NS_ERROR_FAILURE;
    }

    if (!IsValidRilServiceId(id)) {
      return NS_ERROR_UNEXPECTED;
    }

    mRilDataServiceId = id;
    UpdateRadioInterface();

    // Observer may have been addded if RequestDataConnection() called first
    if (mObservingNetworkConnStateChange) {
      return NS_OK;
    }

    // Now we know which service ID to deal with, observe necessary topic then
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    NS_ENSURE_TRUE(obs, NS_OK);

    if (NS_FAILED(obs->AddObserver(this, kNetworkConnStateChangedTopic, false))) {
      NS_WARNING("Failed to add network state changed observer!");
    } else {
      mObservingNetworkConnStateChange = true;
    }
  }
#endif // MOZ_B2G_RIL
  return NS_OK;
}

NS_IMETHODIMP
GonkGPSGeolocationProvider::HandleError(const nsAString& aErrorMessage)
{
  return NS_OK;
}
