/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
<<<<<<< 345fcb382607a7e75422c685e975bdd39b97ad8a
/* (c) 2017 KAI OS TECHNOLOGIES (HONG KONG) LIMITED All rights reserved. This
 * file or any portion thereof may not be reproduced or used in any manner
 * whatsoever without the express written permission of KAI OS TECHNOLOGIES
 * (HONG KONG) LIMITED. KaiOS is the trademark of KAI OS TECHNOLOGIES (HONG KONG)
 * LIMITED or its affiliate company and may be registered in some jurisdictions.
 * All other trademarks are the property of their respective owners.
=======
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
>>>>>>> vt initial version
 */

[Constructor(DOMString type, optional VideoCallCameraCapabilitiesChangeEventInit eventInitDict)]
interface VideoCallCameraCapabilitiesChangeEvent : Event
{
  readonly attribute unsigned short width;
  readonly attribute unsigned short height;
<<<<<<< 345fcb382607a7e75422c685e975bdd39b97ad8a
  readonly attribute unsigned short maxZoom;
  readonly attribute boolean zoomSupported;
=======
>>>>>>> vt initial version
};

dictionary VideoCallCameraCapabilitiesChangeEventInit : EventInit
{
  unsigned short width = 0;
  unsigned short height = 0;
<<<<<<< 345fcb382607a7e75422c685e975bdd39b97ad8a
  float maxZoom = 0.0;
  boolean zoomSupported = false;
=======
>>>>>>> vt initial version
};