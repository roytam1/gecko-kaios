/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeakerManager.h"

#include "mozilla/Preferences.h"
#include "mozilla/Services.h"

#include "mozilla/dom/Event.h"

#include "AudioChannelService.h"
#include "nsIAppsService.h"
#include "nsIDocShell.h"
#include "nsIDOMClassInfo.h"
#include "nsIDOMEventListener.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPermissionManager.h"
#include "SpeakerManagerService.h"

namespace mozilla {
namespace dom {

NS_IMPL_QUERY_INTERFACE_INHERITED(SpeakerManager, DOMEventTargetHelper,
                                  nsIDOMEventListener)
NS_IMPL_ADDREF_INHERITED(SpeakerManager, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(SpeakerManager, DOMEventTargetHelper)

SpeakerManager::SpeakerManager()
  : mForcespeaker(false)
  , mVisible(false)
{
}

SpeakerManager::~SpeakerManager()
{
  SpeakerManagerService *service = SpeakerManagerService::GetOrCreateSpeakerManagerService();
  MOZ_ASSERT(service);

  service->UnRegisterSpeakerManager(this);
  nsCOMPtr<EventTarget> target = do_QueryInterface(GetOwner());
  NS_ENSURE_TRUE_VOID(target);

  target->RemoveSystemEventListener(NS_LITERAL_STRING("visibilitychange"),
                                    this,
                                    /* useCapture = */ true);
}

bool
SpeakerManager::Speakerforced()
{
  // If a background app calls forcespeaker=true that doesn't change anything.
  // 'speakerforced' remains false everywhere.
  if (mForcespeaker && !mVisible) {
    return false;
  }
  SpeakerManagerService *service = SpeakerManagerService::GetOrCreateSpeakerManagerService();
  MOZ_ASSERT(service);
  return service->GetSpeakerStatus();

}

bool
SpeakerManager::Forcespeaker()
{
  return mForcespeaker;
}

void
SpeakerManager::SetForcespeaker(bool aEnable)
{
  SpeakerManagerService *service = SpeakerManagerService::GetOrCreateSpeakerManagerService();
  MOZ_ASSERT(service);

  service->ForceSpeaker(aEnable, mVisible);
  mForcespeaker = aEnable;
}

void
SpeakerManager::DispatchSimpleEvent(const nsAString& aStr)
{
  MOZ_ASSERT(NS_IsMainThread(), "Not running on main thread");
  nsresult rv = CheckInnerWindowCorrectness();
  if (NS_FAILED(rv)) {
    return;
  }

  RefPtr<Event> event = NS_NewDOMEvent(this, nullptr, nullptr);
  event->InitEvent(aStr, false, false);
  event->SetTrusted(true);

  rv = DispatchDOMEvent(nullptr, event, nullptr, nullptr);
  if (NS_FAILED(rv)) {
    NS_ERROR("Failed to dispatch the event!!!");
    return;
  }
}

nsresult
SpeakerManager::FindCorrectWindow(nsPIDOMWindowInner* aWindow)
{
  MOZ_ASSERT(aWindow->IsInnerWindow());

  mWindow = aWindow->GetScriptableTop();
  if (NS_WARN_IF(!mWindow)) {
    return NS_OK;
  }

  // From here we do an hack for nested iframes.
  // The system app doesn't have access to the nested iframe objects so it
  // cannot control the volume of the agents running in nested apps. What we do
  // here is to assign those Agents to the top scriptable window of the parent
  // iframe (what is controlled by the system app).
  // For doing this we go recursively back into the chain of windows until we
  // find apps that are not the system one.
  nsCOMPtr<nsPIDOMWindowOuter> outerParent = mWindow->GetParent();
  if (!outerParent || outerParent == mWindow) {
    return NS_OK;
  }

  nsCOMPtr<nsPIDOMWindowInner> parent = outerParent->GetCurrentInnerWindow();
  if (!parent) {
    return NS_OK;
  }

  nsCOMPtr<nsIDocument> doc = parent->GetExtantDoc();
  if (!doc) {
    return NS_OK;
  }

  nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();

  uint32_t appId;
  nsresult rv = principal->GetAppId(&appId);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (appId == nsIScriptSecurityManager::NO_APP_ID ||
      appId == nsIScriptSecurityManager::UNKNOWN_APP_ID) {
    return NS_OK;
  }

  nsCOMPtr<nsIAppsService> appsService = do_GetService(APPS_SERVICE_CONTRACTID);
  if (NS_WARN_IF(!appsService)) {
    return NS_ERROR_FAILURE;
  }

  nsAdoptingString systemAppManifest =
    mozilla::Preferences::GetString("b2g.system_manifest_url");
  if (!systemAppManifest) {
    return NS_OK;
  }

  uint32_t systemAppId;
  rv = appsService->GetAppLocalIdByManifestURL(systemAppManifest, &systemAppId);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (systemAppId == appId) {
    return NS_OK;
  }

  return FindCorrectWindow(parent);
}

nsresult
SpeakerManager::Init(nsPIDOMWindowInner* aWindow)
{
  BindToOwner(aWindow);

  nsCOMPtr<nsIDocShell> docshell = do_GetInterface(GetOwner());
  NS_ENSURE_TRUE(docshell, NS_ERROR_FAILURE);
  docshell->GetIsActive(&mVisible);

  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(GetOwner());
  NS_ENSURE_TRUE(target, NS_ERROR_FAILURE);

  target->AddSystemEventListener(NS_LITERAL_STRING("visibilitychange"),
                                 this,
                                 /* useCapture = */ true,
                                 /* wantsUntrusted = */ false);

  nsresult rv = FindCorrectWindow(aWindow);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  SpeakerManagerService *service = SpeakerManagerService::GetOrCreateSpeakerManagerService();
  MOZ_ASSERT(service);
  rv = service->RegisterSpeakerManager(this);
  NS_WARN_IF(NS_FAILED(rv));
  return NS_OK;
}

nsPIDOMWindowInner*
SpeakerManager::GetParentObject() const
{
  return GetOwner();
}

/* static */ already_AddRefed<SpeakerManager>
SpeakerManager::Constructor(const GlobalObject& aGlobal, ErrorResult& aRv)
{
  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(aGlobal.GetAsSupports());
  if (!sgo) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsCOMPtr<nsPIDOMWindowInner> ownerWindow = do_QueryInterface(aGlobal.GetAsSupports());
  if (!ownerWindow) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsCOMPtr<nsIPermissionManager> permMgr = services::GetPermissionManager();
  NS_ENSURE_TRUE(permMgr, nullptr);

  uint32_t permission = nsIPermissionManager::DENY_ACTION;
  nsresult rv =
    permMgr->TestPermissionFromWindow(ownerWindow, "speaker-control",
                                      &permission);
  NS_ENSURE_SUCCESS(rv, nullptr);

  if (permission != nsIPermissionManager::ALLOW_ACTION) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  RefPtr<SpeakerManager> object = new SpeakerManager();
  rv = object->Init(ownerWindow);
  if (NS_FAILED(rv)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  return object.forget();
}

JSObject*
SpeakerManager::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return MozSpeakerManagerBinding::Wrap(aCx, this, aGivenProto);
}

NS_IMETHODIMP
SpeakerManager::HandleEvent(nsIDOMEvent* aEvent)
{
  nsAutoString type;
  aEvent->GetType(type);

  if (!type.EqualsLiteral("visibilitychange")) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDocShell> docshell = do_GetInterface(GetOwner());
  NS_ENSURE_TRUE(docshell, NS_ERROR_FAILURE);
  docshell->GetIsActive(&mVisible);

  // If an app that has called forcespeaker=true is switched
  // from the background to the foreground 'speakerforced'
  // switches to true in all apps. I.e. the app doesn't have to
  // call forcespeaker=true again when it comes into foreground.
  SpeakerManagerService *service =
    SpeakerManagerService::GetOrCreateSpeakerManagerService();
  MOZ_ASSERT(service);

  if (mVisible && mForcespeaker) {
    service->ForceSpeaker(mForcespeaker, mVisible);
  }
  // If an application that has called forcespeaker=true, but no audio is
  // currently playing in the app itself, if application switch to
  // the background, we switch 'speakerforced' to false.
  if (!mVisible && mForcespeaker) {
    RefPtr<AudioChannelService> audioChannelService =
      AudioChannelService::GetOrCreate();
    if (audioChannelService && !audioChannelService->AnyAudioChannelIsActive()) {
      service->ForceSpeaker(false, mVisible);
    }
  }
  return NS_OK;
}

void
SpeakerManager::SetAudioChannelActive(bool isActive)
{
  // - When |mVisible| is true:
  //   It should always respect |mForcespeaker|, no matter what
  //   audio channel state is. So no need to call ForceSpeaker()
  //   here and just let HandleEvent() handle visibility change.
  // - When |mVisible| is false:
  //   Only need to disable ForceSpeaker when our audio channel
  //   is interrupted by others.
  if (mForcespeaker && !mVisible && !isActive) {
    SpeakerManagerService *service =
      SpeakerManagerService::GetOrCreateSpeakerManagerService();
    MOZ_ASSERT(service);
    service->ForceSpeaker(isActive, mVisible);
  }
}

} // namespace dom
} // namespace mozilla
