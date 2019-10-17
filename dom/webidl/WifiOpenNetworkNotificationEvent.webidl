/* (c) 2017 KAI OS TECHNOLOGIES (HONG KONG) LIMITED All rights reserved. This
* file or any portion thereof may not be reproduced or used in any manner
* whatsoever without the express written permission of KAI OS TECHNOLOGIES
* (HONG KONG) LIMITED. KaiOS is the trademark of KAI OS TECHNOLOGIES (HONG KONG)
* LIMITED or its affiliate company and may be registered in some jurisdictions.
* All other trademarks are the property of their respective owners.
*/

[Constructor(DOMString type, optional WifiOpenNetworkNotificationEventInit eventInitDict)]
interface WifiOpenNetworkNotificationEvent : Event
{
  /**
   * Open network notification is enabled or not.
   */
  readonly attribute boolean enabled;
};

dictionary WifiOpenNetworkNotificationEventInit : EventInit
{
  boolean enabled = false;
};
