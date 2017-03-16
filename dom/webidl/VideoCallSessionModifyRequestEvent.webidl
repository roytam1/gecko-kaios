/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

[Constructor(DOMString type, optional VideoCallSessionModifyRequestEventInit eventInitDict)]
interface VideoCallSessionModifyRequestEvent : Event
{
<<<<<<< 345fcb382607a7e75422c685e975bdd39b97ad8a
  readonly attribute any request;
=======
  readonly attribute VideoCallProfile? request;
>>>>>>> vt initial version
};

dictionary VideoCallSessionModifyRequestEventInit : EventInit
{
<<<<<<< 345fcb382607a7e75422c685e975bdd39b97ad8a
  any request = null;
=======
  VideoCallProfile? request = null;
>>>>>>> vt initial version
};