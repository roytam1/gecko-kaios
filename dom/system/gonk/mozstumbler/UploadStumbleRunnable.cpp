/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UploadStumbleRunnable.h"
#include "StumblerLogging.h"
#include "mozilla/dom/Event.h"
#include "mozilla/Preferences.h"
#include "nsContentUtils.h"
#include "nsIInputStream.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURLFormatter.h"
#include "nsIXMLHttpRequest.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsVariant.h"

// The cache of access token for using location HTTP API
static nsCString sAccessToken;

UploadStumbleRunnable::UploadStumbleRunnable(nsIInputStream* aUploadData)
: mUploadInputStream(aUploadData)
, mNeedAuthorization(false)
{
  mNeedAuthorization = Preferences::GetBool("geo.provider.need_authorization");
}

NS_IMETHODIMP
UploadStumbleRunnable::Run()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mNeedAuthorization && sAccessToken.IsEmpty()) {
    // Upload() will be called once we get the token from setting callback.
    RequestSettingValue("geolocation.kaios.accessToken");
  } else {
    nsresult rv = Upload();
    if (NS_FAILED(rv)) {
      WriteStumbleOnThread::UploadEnded(false);
    }
  }
  return NS_OK;
}

nsresult
UploadStumbleRunnable::Upload()
{
  nsresult rv;
  RefPtr<nsVariant> variant = new nsVariant();

  rv = variant->SetAsISupports(mUploadInputStream);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIXMLHttpRequest> xhr = do_CreateInstance(NS_XMLHTTPREQUEST_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIScriptSecurityManager> secman =
    do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> systemPrincipal;
  rv = secman->GetSystemPrincipal(getter_AddRefs(systemPrincipal));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = xhr->Init(systemPrincipal, nullptr, nullptr, nullptr, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURLFormatter> formatter =
    do_CreateInstance("@mozilla.org/toolkit/URLFormatterService;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsString url;
  rv = formatter->FormatURLPref(NS_LITERAL_STRING("geo.stumbler.url"), url);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = xhr->Open(NS_LITERAL_CSTRING("POST"), NS_ConvertUTF16toUTF8(url), false, EmptyString(), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Type"), NS_LITERAL_CSTRING("gzip"));
  if (mNeedAuthorization) {
    xhr->SetRequestHeader(NS_LITERAL_CSTRING("Authorization"), NS_LITERAL_CSTRING("Bearer ") + sAccessToken);
  }
  xhr->SetMozBackgroundRequest(true);
  // 60s timeout
  xhr->SetTimeout(60 * 1000);

  nsCOMPtr<EventTarget> target(do_QueryInterface(xhr));
  RefPtr<nsIDOMEventListener> listener = new UploadEventListener(xhr);

  const char* const sEventStrings[] = {
    // nsIXMLHttpRequestEventTarget event types
    "abort",
    "error",
    "load",
    "timeout"
  };

  for (uint32_t index = 0; index < MOZ_ARRAY_LENGTH(sEventStrings); index++) {
    nsAutoString eventType = NS_ConvertASCIItoUTF16(sEventStrings[index]);
    rv = target->AddEventListener(eventType, listener, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = xhr->Send(variant);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(UploadEventListener, nsIDOMEventListener)

UploadEventListener::UploadEventListener(nsIXMLHttpRequest* aXHR)
: mXHR(aXHR)
{
}

NS_IMETHODIMP
UploadEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  nsString type;
  if (NS_FAILED(aEvent->GetType(type))) {
    STUMBLER_ERR("Failed to get event type");
    WriteStumbleOnThread::UploadEnded(false);
    return NS_ERROR_FAILURE;
  }

  if (type.EqualsLiteral("load")) {
    STUMBLER_DBG("Got load Event\n");
  } else if (type.EqualsLiteral("error") && mXHR) {
    STUMBLER_ERR("Upload Error");
  } else {
    STUMBLER_DBG("Receive %s Event", NS_ConvertUTF16toUTF8(type).get());
  }

  uint32_t statusCode = 0;
  bool doDelete = false;
  if (!mXHR) {
    return NS_OK;
  }
  nsresult rv = mXHR->GetStatus(&statusCode);
  if (NS_SUCCEEDED(rv)) {
    STUMBLER_DBG("statuscode %d\n", statusCode);
  }

  if (200 == statusCode || 400 == statusCode) {
    doDelete = true;
  }

  WriteStumbleOnThread::UploadEnded(doDelete);
  nsCOMPtr<EventTarget> target(do_QueryInterface(mXHR));

  const char* const sEventStrings[] = {
    // nsIXMLHttpRequestEventTarget event types
    "abort",
    "error",
    "load",
    "timeout"
  };

  for (uint32_t index = 0; index < MOZ_ARRAY_LENGTH(sEventStrings); index++) {
    nsAutoString eventType = NS_ConvertASCIItoUTF16(sEventStrings[index]);
    rv = target->RemoveEventListener(eventType, this, false);
  }

  mXHR = nullptr;
  return NS_OK;
}

NS_IMPL_ISUPPORTS(UploadStumbleRunnable,
                  nsISettingsServiceCallback)

/** nsISettingsServiceCallback **/

NS_IMETHODIMP
UploadStumbleRunnable::Handle(const nsAString& aName,
                              JS::Handle<JS::Value> aResult)
{
  if (aName.EqualsLiteral("geolocation.kaios.accessToken")) {
    if (aResult.isString()) {
      JSContext *cx = nsContentUtils::GetCurrentJSContext();
      NS_ENSURE_TRUE(cx, NS_OK);

      nsAutoJSString token;
      if (!token.init(cx, aResult.toString())) {
        WriteStumbleOnThread::UploadEnded(false);
        return NS_ERROR_FAILURE;
      }
      if (!token.IsEmpty()) {
        // Get token from settings value.
        sAccessToken = NS_ConvertUTF16toUTF8(token);;

        // Upload the StumblerInfo with the access token.
        nsresult rv = Upload();
        if (NS_FAILED(rv)) {
          WriteStumbleOnThread::UploadEnded(false);
        }
      } else {
        WriteStumbleOnThread::UploadEnded(false);
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
UploadStumbleRunnable::HandleError(const nsAString& aErrorMessage)
{
  WriteStumbleOnThread::UploadEnded(false);
  return NS_OK;
}

void
UploadStumbleRunnable::RequestSettingValue(const char* aKey)
{
  MOZ_ASSERT(aKey);
  nsCOMPtr<nsISettingsService> ss = do_GetService("@mozilla.org/settingsService;1");
  if (!ss) {
    MOZ_ASSERT(ss);
    return;
  }

  nsCOMPtr<nsISettingsServiceLock> lock;
  nsresult rv = ss->CreateLock(nullptr, getter_AddRefs(lock));
  if (NS_FAILED(rv)) {
    nsContentUtils::LogMessageToConsole(nsPrintfCString(
      "geo: error while createLock setting '%s': %d\n", aKey, rv).get());
    return;
  }

  rv = lock->Get(aKey, this);
  if (NS_FAILED(rv)) {
    nsContentUtils::LogMessageToConsole(nsPrintfCString(
      "geo: error while get setting '%s': %d\n", aKey, rv).get());
    return;
  }
}
