/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_powersupply_Constants_h__
#define mozilla_dom_powersupply_Constants_h__

/**
 * A set of constants that might need to be used by powersupply backends.
 * It's not part of PowerSupplyManager.h to prevent those backends to include it.
 */
namespace mozilla {
namespace dom {
namespace powersuppply {

  static const bool powerSupplyOnline         = false;
  static const char* powerSupplyType          = "Unknown";

} // namespace powersuppply
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_powersuppply_Constants_h__
