/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_usb_Constants_h__
#define mozilla_dom_usb_Constants_h__

/**
 * A set of constants that might need to be used by usb backends.
 * It's not part of UsbManager.h to prevent those backends to include it.
 */
namespace mozilla {
namespace dom {
namespace usb {

  static const bool deviceAttached         = false;
  static const bool deviceConfigured       = false;

} // namespace usb
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_usb_Constants_h__
