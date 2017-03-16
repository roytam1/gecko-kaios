/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

enum VideoCallSessionStatus {
  // reserved.
  "unknown",
  // request success.
  "success",
  // request failed.
  "fail",
  // request is invalid.
  "invalid",
  // request is timed-out.
  "timed-out",
  // request is rejected by remote.
  "rejected-by-remote"
};

[Constructor(DOMString type, optional VideoCallSessionModifyResponseEventInit eventInitDict)]
interface VideoCallSessionModifyResponseEvent : Event
{
  readonly attribute VideoCallSessionStatus status;
<<<<<<< 345fcb382607a7e75422c685e975bdd39b97ad8a
  readonly attribute any request;
  readonly attribute any response;
=======
  readonly attribute VideoCallProfile? request;
  readonly attribute VideoCallProfile? response;
>>>>>>> vt initial version
};

dictionary VideoCallSessionModifyResponseEventInit : EventInit
{
  VideoCallSessionStatus status = "unknown";
<<<<<<< 345fcb382607a7e75422c685e975bdd39b97ad8a
  any request = null;
  any response = null;
=======
  VideoCallProfile? request = null;
  VideoCallProfile? response = null;
>>>>>>> vt initial version
};