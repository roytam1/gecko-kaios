/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_rt9connect_IMEConnect_h
#define mozilla_dom_rt9connect_IMEConnect_h

#include "nsCOMPtr.h"
#include "nsPIDOMWindow.h"
#include "nsISupports.h"
#include "nsWrapperCache.h"
#include "nsCycleCollectionParticipant.h"
#include "nsAutoPtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "MainThreadUtils.h"

extern "C" {
#include "keypad_R9.h"
}

#include <sys/time.h>
#include <stdio.h>

#include <string.h>
#include <errno.h>

#if ANDROID_VERSION >= 23
#include <algorithm>
#else
#include <stl/_algo.h>
#endif

#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG_RT9_TAG "GeckoRt9Connect"
#define RT9_DEBUG 0

#define RT9_LOGW(args...)  __android_log_print(ANDROID_LOG_WARN, LOG_RT9_TAG , ## args)
#define RT9_LOGE(args...)  __android_log_print(ANDROID_LOG_ERROR, LOG_RT9_TAG , ## args)

#if RT9_DEBUG
#define RT9_LOGD(args...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_RT9_TAG , ## args)
#else
#define RT9_LOGD(args...)
#endif

#else

#define RT9_LOGD(args...)
#define RT9_LOGW(args...)
#define RT9_LOGE(args...)

#endif

namespace mozilla {
namespace dom {

static const int BUFFER_LEN_MAX     = 30;

typedef struct {
  unsigned char psBuffer[30];
  int snBufferLen;
  int snCursorPos;
} demoEditorInfo;


void EditorInitEmptyWord(demoEditorInfo * const pEditor);
void clearAllSymbsChar(unsigned char *psBuffer);
void clearAllSymbsShort(unsigned short *buffer);
void clearAllWordString();
void EditorMoveBackward(demoEditorInfo * const pEditor);
void EditorMoveForward(demoEditorInfo * const pEditor);
void updateCandidateWord(demoEditorInfo * const pEditor);
int updatesymbol(demoEditorInfo * const pEditor,const unsigned char ch);
int DeleteSymbols(demoEditorInfo * const pEditor);
void copyShorttoUTF16(unsigned short *mCandidateWord, nsAString& aResult);

class IMEConnect final : public nsISupports, public nsWrapperCache
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(IMEConnect)

  static already_AddRefed<IMEConnect>
  Constructor(const GlobalObject& aGlobal,
              ErrorResult& aRv);

  static already_AddRefed<IMEConnect>
  Constructor(const GlobalObject& aGlobal,
              uint32_t aLID,
              ErrorResult& aRv);

  bool InitEmptyWord() const
  {
    RT9_LOGD("This is a dummy fun for InitEmptyWord");
    return (true);
  }

  void SetInitEmptyWord(const bool aResult)
  {
    RT9_LOGD("This is a dummy fun for SetInitEmptyWord");
  }

  void GetWholeWord(nsAString& aResult)
  {
    RT9_LOGD("This is a dummy fun for GetWholeWord");
  }

  void SetWholeWord(const nsAString& aResult)
  {
    RT9_LOGD("This is a dummy fun for SetWholeWord");
  }

  void GetCandidateWord(nsAString& aResult)
  {
    copyShorttoUTF16(mCandidateWord, aResult);
  }

  uint16_t TotalWord() const
  {
    return mTotalWord;
  }

  uint32_t CursorPosition() const
  {
    RT9_LOGD("This is a dummy fun for CursorPosition");
    return 0;
  }

  void SetCursorPosition(const uint32_t aResult)
  {
    RT9_LOGD("This is a dummy fun for SetCursorPosition");
  }

  uint32_t CurrentLID() const
  {
    return mCurrentLID;
  }

  void GetName(nsAString& aName) const
  {
    CopyASCIItoUTF16("RT9", aName);
  }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  nsPIDOMWindowInner* GetOwner() const { return mWindow; }

  nsPIDOMWindowInner* GetParentObject()
  {
    return GetOwner();
  }

  explicit IMEConnect(nsPIDOMWindowInner* aWindow);

  nsresult Init(uint32_t aLID);

  static void SetLetter(const unsigned long aHexPrefix, const unsigned long aHexLetter, ErrorResult& aRv);
  static uint32_t SetLanguage(const uint32_t lid);

  static unsigned short mCandidateWord[200];
  static uint16_t  mTotalWord;
  static uint16_t  lenCandidateWord;
  static uint32_t mCurrentLID;

private:
  ~IMEConnect();
};
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_rt9connect_IMEConnect_h
