/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * Provide an Android-Surface-like Surface to JS APP, this will create a correspond Android Surface in native side.
 */
[Func="nsDOMSurfaceControl::HasSupport"]
interface SurfaceControl : MediaStream
{
};