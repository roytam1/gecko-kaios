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

let device_info_cache;

this.DeviceUtils = {
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

  getTDeviceObject: function DeviceUtils_getTDeviceObject() {
    if (device_info_cache) {
      return Promise.resolve(device_info_cache);
    }

    let device_info;
    if (isGonk) {
      let characteristics = libcutils.property_get("ro.build.characteristics");

      device_info = {
        reference: this.getRefNumber(),
        os: Services.prefs.getCharPref("b2g.osName"),
        os_version: Services.prefs.getCharPref("b2g.version"),
        device_type: device_type_map[characteristics],
        brand: libcutils.property_get("ro.product.brand"),
        model: libcutils.property_get("ro.product.name")
      };
    } else {
      return Promise.reject();
    }

    // TODO: need to check how to handle dual-SIM case.
    if (typeof gMobileConnectionService != "undefined") {
      let conn = gMobileConnectionService.getItemByServiceId(0);
      conn.getDeviceIdentities({
        notifyGetDeviceIdentitiesRequestSuccess: function(aResult) {
          if (aResult.imei && parseInt(aResult.imei) !== 0) {
            device_info.device_id = aResult.imei;
          } else if (aResult.meid && parseInt(aResult.meid) !== 0) {
            device_info.device_id = aResult.meid;
          } else if (aResult.esn && parseInt(aResult.esn) !== 0) {
            device_info.device_id = aResult.esn;
          } else {
            return Promise.reject();
          }
        }
      });
    } else {
      return Promise.reject();
    }
    device_info_cache = device_info;
    return Promise.resolve(device_info);
  },
};
