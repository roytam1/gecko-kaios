/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

this.EXPORTED_SYMBOLS = [ "DeviceUtils" ];

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Promise.jsm");
Cu.import("resource://gre/modules/AppConstants.jsm");

const isGonk = AppConstants.platform === 'gonk';

if (isGonk) {
  XPCOMUtils.defineLazyModuleGetter(this, "libcutils", "resource://gre/modules/systemlibs.js");
}

XPCOMUtils.defineLazyServiceGetter(this, "gMobileConnectionService",
                                   "@mozilla.org/mobileconnection/mobileconnectionservice;1",
                                   "nsIMobileConnectionService");
const REQUEST_REJECT = 0;
const HTTP_CODE_OK = 200;
const HTTP_CODE_CREATED = 201;
const HTTP_CODE_BAD_REQUEST = 400;

XPCOMUtils.defineLazyGetter(this, "console", () => {
  let {ConsoleAPI} = Cu.import("resource://gre/modules/Console.jsm", {});
  return new ConsoleAPI({
    maxLogLevelPref: "toolkit.deviceUtils.loglevel",
    prefix: "DeviceUtils",
  });
});

const device_type_map = {
  default: 1000,
  feature_phone: 1000,
  phone: 2000,
  tablet: 3000,
  watch: 4000
}

this.DeviceUtils = {
  device_info_cache: null,
  /**
   * Returns a device reference number which is vendor dependent.
   * For developer, a pref could be set from device config folder.
   */
  getRefNumber: function DeviceUtils_getRefNumber() {
    let cuRefStr;

    try {
      if (isGonk) {
        cuRefStr = libcutils.property_get("ro.fota.cu_ref");
      }
      if (!cuRefStr) {
        cuRefStr = Services.prefs.getCharPref("device.cuRef.default");
      }
    } catch(e) {
      dump("DeviceUtils.getRefNumber error=" + e);
    }
    return cuRefStr;
  },

  getDeviceId: function DeviceUtils_getDeviceId() {
    let deferred = Promise.defer();
    // TODO: need to check how to handle dual-SIM case.
    if (typeof gMobileConnectionService != "undefined") {
      let conn = gMobileConnectionService.getItemByServiceId(0);
      conn.getDeviceIdentities({
        notifyGetDeviceIdentitiesRequestSuccess: function(aResult) {
          if (aResult.imei && parseInt(aResult.imei) !== 0) {
            deferred.resolve(aResult.imei);
          } else if (aResult.meid && parseInt(aResult.meid) !== 0) {
            deferred.resolve(aResult.meid);
          } else if (aResult.esn && parseInt(aResult.esn) !== 0) {
            deferred.resolve(aResult.esn);
          } else {
            deferred.reject();
          }
        }
      });
    } else {
      deferred.reject();
    }
    return deferred.promise;
  },

  getTDeviceObject: function DeviceUtils_getTDeviceObject() {
    let deferred = Promise.defer();
    if (this.device_info_cache) {
      deferred.resolve(this.device_info_cache);
    } else {
      this.getDeviceId().then(
        device_id => {
          if (isGonk) {
            let characteristics = libcutils.property_get("ro.build.characteristics");
            if (!(characteristics in device_type_map)) {
              characteristics = "default";
            }
            let device_info = {
              device_id: device_id,
              reference: this.getRefNumber(),
              os: Services.prefs.getCharPref("b2g.osName"),
              os_version: Services.prefs.getCharPref("b2g.version"),
              device_type: device_type_map[characteristics],
              brand: libcutils.property_get("ro.product.brand"),
              model: libcutils.property_get("ro.product.name")
            };

            this.device_info_cache = device_info;
            deferred.resolve(device_info);
          } else {
            deferred.reject();
          }
        },
        error => {
          deferred.reject();
        }
      );
    }
    return deferred.promise;
  },

  /**
   * Fetch access token from Restricted Token Server
   *
   * @param url
   *        A Restricted Application Access URL including app_id
   *        like Push or LBS
   * @param apiKeyName
   *        An indicator for getting API key by urlFormatter
   *
   * @return Promise
   *        Returns a promise that resolves to a JSON object
   *        from server:
   *        {
   *          access_token: A JWT token for restricted access a service
   *          token_type: Token type. E.g. bearer
   *          scope: Assigned to client by restricted access token server
   *          expires_in: Time To Live of the current credential (in seconds)
   *        }
   *        Returns a promise that rejects to an error status
   */
  fetchAccessToken: function DeviceUtils_fetchAccessToken(url, apiKeyName) {
    if (typeof url !== 'string' || url.length == 0 ||
      typeof apiKeyName !== 'string' || apiKeyName.length == 0) {
      console.error("Invalid Input");
      return Promise.reject(REQUEST_REJECT);
    }

    let deferred = Promise.defer();
    this.getTDeviceObject().then((device_info) => {
      let xhr = Components.classes["@mozilla.org/xmlextras/xmlhttprequest;1"]
                          .createInstance(Ci.nsIXMLHttpRequest);
      try {
        xhr.open("POST", url, true);
      } catch (e) {
        deferred.reject(REQUEST_REJECT);
        return;
      }

      xhr.setRequestHeader("Content-Type", "application/json; charset=UTF-8");

      let authorizationKey;
      try {
        authorizationKey = Services.urlFormatter.formatURL(apiKeyName);
      } catch (e) {
        deferred.reject(REQUEST_REJECT);
        return;
      }

      if (typeof authorizationKey !== 'string' ||
        authorizationKey.length == 0 ||
        authorizationKey == 'no-kaios-api-key') {
        deferred.reject(REQUEST_REJECT);
        console.error("without API Key");
        return;
      }

      xhr.setRequestHeader("Authorization", "Key " + authorizationKey);

      xhr.responseType = "json";
      xhr.mozBackgroundRequest = true;
      xhr.channel.loadFlags = Ci.nsIChannel.LOAD_ANONYMOUS;
      // Prevent the request from reading from the cache.
      xhr.channel.loadFlags |= Ci.nsIRequest.LOAD_BYPASS_CACHE;
      // Prevent the request from writing to the cache.
      xhr.channel.loadFlags |= Ci.nsIRequest.INHIBIT_CACHING;
      xhr.onerror = (function () {
        if (xhr.status > 0) {
          deferred.reject(xhr.status);
        } else {
          deferred.reject(REQUEST_REJECT);
        }
        console.error("An error occurred during the transaction, status: " + xhr.status);
      }).bind(this);

      xhr.onload = (function () {
        console.debug("Get access token returned status: " + xhr.status);
        // only accept status code 200 and 201.
        let isStatusInvalid = xhr.channel instanceof Ci.nsIHttpChannel &&
          (xhr.status != HTTP_CODE_OK && xhr.status != HTTP_CODE_CREATED);
        if (isStatusInvalid || !xhr.response || !xhr.response.access_token) {
          deferred.reject(xhr.status);
          console.debug("Response: " + JSON.stringify(xhr.response));
        } else {
          deferred.resolve(xhr.response);
        }
      }).bind(this);

      var requestData = JSON.stringify(device_info);
      console.debug("Refresh access token by sending: " + requestData);
      xhr.send(requestData);
    }, _ => {
      deferred.reject(REQUEST_REJECT);
      console.error("An error occurred during getting device info");
    });

    return deferred.promise;
  },
};
