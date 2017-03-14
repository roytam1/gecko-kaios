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
#ifdef PROPRIETARY_JRD_SERVICE
Cu.import('resource://gre/modules/jrd_service.jsm');
#endif

const PREF_SERVICE_DEVICE_TYPE = "services.kaiostech.device_type";
const PREF_SERVICE_DEVICE_BRAND = "services.kaiostech.brand";
const PREF_SERVICE_DEVICE_MODEL = "services.kaiostech.model";

XPCOMUtils.defineLazyServiceGetter(this, "gMobileConnectionService",
                                   "@mozilla.org/mobileconnection/mobileconnectionservice;1",
                                   "nsIMobileConnectionService");

let device_info;

this.DeviceUtils = {

  /**
   * Returns a device reference number which is vendor dependent.
   * For developer, a pref could be set from device config folder.
   */
  getRefNumber: function DeviceUtils_getRefNumber() {
    let cuRefStr;

    try {
      if (typeof JrdService != "undefined") {
        let cuRef = JrdService._readNvitem(55, null);
        if (cuRef.result == 'OK') {
          cuRefStr = CommonUtils.byteArrayToString(cuRef.data).replace(/\0/g,'');
        }
      } else {
          cuRefStr = Services.prefs.getCharPref("device.cuRef.default");
      }
    } catch(e) {}
    return cuRefStr;
  },

  initTDeviceObject: function DeviceUtils_initTDeviceObject() {
    if (!device_info) {
      device_info = {
        reference: this.getRefNumber(),
        os: Services.prefs.getCharPref("b2g.osName"),
        os_version: Services.prefs.getCharPref("b2g.version"),
      };
      try {
        device_info.device_type = Services.prefs.getIntPref(PREF_SERVICE_DEVICE_TYPE);
        device_info.brand       = Services.prefs.getCharPref(PREF_SERVICE_DEVICE_BRAND);
        device_info.model       = Services.prefs.getCharPref(PREF_SERVICE_DEVICE_MODEL);
      } catch (e) {}
    }
  },

  getTDeviceObject: function DeviceUtils_getTDeviceObject() {
    let deferred = Promise.defer();
    this.initTDeviceObject();
    // TODO: need to check how to handle dual-SIM case.
    if (typeof device_info.device_id != "undefined") {
      deferred.resolve(device_info);
    }
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
            deferred.reject();
            return;
          }
          deferred.resolve(device_info);
        }
      });
    } else {
      deferred.reject();
    }
    return deferred.promise;
  },
};
