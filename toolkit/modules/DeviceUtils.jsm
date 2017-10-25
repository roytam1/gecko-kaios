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
};
