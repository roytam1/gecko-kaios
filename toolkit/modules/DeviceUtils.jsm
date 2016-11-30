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
#ifdef PROPRIETARY_JRD_SERVICE
Cu.import('resource://gre/modules/jrd_service.jsm');
#endif

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
  }
};
