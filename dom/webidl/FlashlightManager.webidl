/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * FlashlightManager reports the current flashlight status, and dispatch a
 * flashlightchange event when device has flashlight opened or closed.
 */
[Pref="dom.flashlight.enabled", CheckAnyPermissions="flashlight", AvailableIn=CertifiedApps]
interface FlashlightManager : EventTarget {
    attribute boolean flashlightEnabled;

    attribute EventHandler onflashlightchange;
};
