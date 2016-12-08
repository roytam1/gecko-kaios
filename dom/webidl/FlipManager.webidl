/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * FlipManager reports the current flip status, and dispatch a flipchange event
 * when device has flip opened or closed.
 */
[Pref="dom.flip.enabled", CheckAnyPermissions="flip", AvailableIn=CertifiedApps]
interface FlipManager : EventTarget {
    readonly attribute boolean flipOpened;

    attribute EventHandler onflipchange;
};
