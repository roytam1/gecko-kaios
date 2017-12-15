/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* (c) 2017 KAI OS TECHNOLOGIES (HONG KONG) LIMITED All rights reserved. This
 * file or any portion thereof may not be reproduced or used in any manner
 * whatsoever without the express written permission of KAI OS TECHNOLOGIES
 * (HONG KONG) LIMITED. KaiOS is the trademark of KAI OS TECHNOLOGIES (HONG KONG)
 * LIMITED or its affiliate company and may be registered in some jurisdictions.
 * All other trademarks are the property of their respective owners.
 */

[JSImplementation="@mozilla.org/kaiauth/authorization-manager;1",
 NoInterfaceObject,
 NavigatorProperty="kaiAuth",
 CheckAnyPermissions="cloud-authorization",
 Pref="dom.kaiauth.enabled"]
interface AuthorizationManager {
  /**
   * Get a restricted access token from cloud server via HTTPS.
   *
   * @param type
   *        Specify the cloud service you're asking for authorization
   * @return A promise object.
   *  Resolve params: a DOMString represents an access token
   *  Reject params:  a integer represents HTTP status code
   */
  Promise<DOMString> getRestrictedToken(KaiServiceType type);
};

/**
 * Types of cloud service which require access token.
 * The uri (end point) and API key of individual service are configured by
 * Gecko Preference.
 */
enum KaiServiceType
{
  "apps",     // configured by apps.token.uri, apps.authorization.key
  "metrics"	  // configured by metrics.token.uri, metrics.authorization.key
};
