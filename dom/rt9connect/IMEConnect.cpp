/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/IMEConnect.h"
#include "mozilla/dom/IMEConnectBinding.h"

namespace mozilla {
namespace dom {

static demoEditorInfo pEditor = {{0},0,0};

unsigned short words_get[30][30];
unsigned int numberOfCandidateWords;
unsigned int pageCount = 0;
unsigned int pageCursor = 0;

unsigned short IMEConnect::mCandidateWord[200];
uint16_t IMEConnect::mTotalWord;
uint16_t IMEConnect::lenCandidateWord;
uint32_t IMEConnect::mCurrentLID;

void EditorInitEmptyWord(demoEditorInfo * const pEditor)
{
  pEditor->snCursorPos = pEditor->snBufferLen = 0;
  clearAllSymbsChar(pEditor->psBuffer);
  clearAllSymbsShort(IMEConnect::mCandidateWord);
  clearAllWordString();
  IMEConnect::mTotalWord = 0;
  pageCount = 0;
  pageCursor = 1;
  IMEConnect::lenCandidateWord = 0;
}

void clearAllSymbsChar(unsigned char *psBuffer)
{
  memset(psBuffer,'\0',BUFFER_LEN_MAX);
}

void clearAllSymbsShort(unsigned short *buffer)
{
  memset(buffer,0,200);
}

void clearAllWordString()
{
  int i;
  for (i=0; i<30; i++) {
    memset(words_get[i],0,30);
  }
}

void EditorMoveBackward(demoEditorInfo * const pEditor)
{
  if (pEditor->snCursorPos > 0) {
    --pEditor->snCursorPos;

    if (pageCursor > 0) {
      pageCursor--;
    } else if (pageCursor == 0 && pageCount > 0) {
      pageCursor = 4;
      pageCount--;
    }

    updateCandidateWord(pEditor);
  } else if (pEditor->snCursorPos == 0 && pageCursor == 1 && pageCount == 0) {
    pageCursor--;
    updateCandidateWord(pEditor);
  } else {
    pEditor->snCursorPos = IMEConnect::mTotalWord -1;
    pageCursor = IMEConnect::mTotalWord%5;
    pageCount = IMEConnect::mTotalWord/5;
    updateCandidateWord(pEditor);
  }
}

void EditorMoveForward(demoEditorInfo * const pEditor)
{
  if (pEditor->snCursorPos < IMEConnect::mTotalWord-1) {
    ++pEditor->snCursorPos;

    if (pageCursor >= 4) {
      pageCursor = 0;
      pageCount++;
    } else {
      pageCursor++;
    }

    updateCandidateWord(pEditor);
  } else {
    pEditor->snCursorPos = 0;
    pageCursor = 0;
    pageCount = 0;
    updateCandidateWord(pEditor);
  }
}

void updateCandidateWord(demoEditorInfo * const pEditor)
{
  int i,j,c=0;
  unsigned int wordCount = 0;
  int startIndex=0;
  clearAllSymbsShort(IMEConnect::mCandidateWord);

  if (IMEConnect::mTotalWord > 0) {
    if (pageCount ==0) {
      if (pageCursor == 0) {
        IMEConnect::mCandidateWord[c++] = '>';
      }
      for (i=0; i<pEditor->snBufferLen; i++) {
        IMEConnect::mCandidateWord[c++] = (unsigned short)pEditor->psBuffer[i];
      }
      IMEConnect::mCandidateWord[c++] = ' ';
      wordCount++;
    }

    if(pageCount == 0) {
      startIndex = 0;
    } else {
      startIndex = (pageCount*5) - 1;
    }

    for (i=startIndex; i<IMEConnect::mTotalWord; i++) {
      if (pageCursor == wordCount) {
        IMEConnect::mCandidateWord[c++] = '>';
      }

      j = 0;
      while (words_get[i][j]) {
        IMEConnect::mCandidateWord[c++] = words_get[i][j++];
      }
      IMEConnect::mCandidateWord[c++] = ' ';
      wordCount++;
      if(wordCount == 5) {
        break;
      }
    }
    IMEConnect::lenCandidateWord = c;
  }
}

int updatesymbol(demoEditorInfo * const pEditor,const unsigned char ch)
{
  if (pEditor->snBufferLen >= BUFFER_LEN_MAX) {
    return(-1);
  } else {
    pEditor->psBuffer[pEditor->snBufferLen] = ch;
    pEditor->snBufferLen++;
    pEditor->snCursorPos = 0;
  }
  return 0;
}

int DeleteSymbols(demoEditorInfo * const pEditor)
{
  if (pEditor->snBufferLen > 0) {
    pEditor->psBuffer[pEditor->snBufferLen-1] = 0;
    pEditor->snBufferLen--;
    pEditor->snCursorPos = 0;
  } else {
    return (-1);
  }
  return 0;
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(IMEConnect, mWindow)
NS_IMPL_CYCLE_COLLECTING_ADDREF(IMEConnect)
NS_IMPL_CYCLE_COLLECTING_RELEASE(IMEConnect)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IMEConnect)
NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

IMEConnect::IMEConnect(nsPIDOMWindowInner* aWindow): mWindow(aWindow)
{
  MOZ_ASSERT(mWindow);
}


already_AddRefed<IMEConnect>
IMEConnect::Constructor(const GlobalObject& aGlobal,
                        ErrorResult& aRv)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<IMEConnect> rt_object = new IMEConnect(window);
  aRv = rt_object->Init(English);

  return rt_object.forget();
}

already_AddRefed<IMEConnect>
IMEConnect::Constructor(const GlobalObject& aGlobal,
                        uint32_t aLID,
                        ErrorResult& aRv)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<IMEConnect> rt_object = new IMEConnect(window);
  aRv = rt_object->Init(aLID);

  return rt_object.forget();
}

nsresult
IMEConnect::Init(uint32_t aLID)
{
  //reverieSetLanguage((LanguageIndex)aLID,&status);
  return NS_OK;
}

void IMEConnect::SetLetter(const unsigned long aHexPrefix, const unsigned long aHexLetter, ErrorResult& aRv)
{
  int Status,count;

  if  (aHexLetter == 0x1B) {
    return;
  }

  if (aHexPrefix == 224) {
    switch (aHexLetter) {
      case 72: // left arrow -
        EditorMoveBackward(&pEditor);
        break;
      case 80: // right arrow -
        EditorMoveForward(&pEditor);
        break;
      default:
        break;
    }
  } else {
    switch (aHexLetter) {
      case 8: // delete(back space)
        DeleteSymbols(&pEditor);
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 13: //enter
        pageCount = 0;
        pageCursor = 1;
        EditorInitEmptyWord(&pEditor);
        break;
      case 49:
        updatesymbol(&pEditor,'1');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 50:
        updatesymbol(&pEditor,'2');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 51:
        updatesymbol(&pEditor,'3');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 52:
        updatesymbol(&pEditor,'4');
        reverieGetPredictedWords(pEditor.psBuffer,
		                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 53:
        updatesymbol(&pEditor,'5');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 54:
        updatesymbol(&pEditor,'6');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 55:
        updatesymbol(&pEditor,'7');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 56:
        updatesymbol(&pEditor,'8');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 57:
        updatesymbol(&pEditor,'9');
        reverieGetPredictedWords(pEditor.psBuffer,
                                 pEditor.snBufferLen,
                                 words_get,
                                 &count,
                                 &Status);
        IMEConnect::mTotalWord = count;
        pageCount = 0;
        pageCursor = 1;
        updateCandidateWord(&pEditor);
        break;
      case 58:
        pageCount = 0;
        pageCursor = 1;
        EditorInitEmptyWord(&pEditor);
        break;
    }
  }
}

uint32_t IMEConnect::SetLanguage(const uint32_t lid)
{
  int status;
  reverieSetLanguage((LanguageIndex)lid,&status);
  reverieSetHalfWord(1);

  if(status == 0) {
    IMEConnect::mCurrentLID = lid;
    EditorInitEmptyWord(&pEditor);
  } else {
    RT9_LOGE("Init::Language_load error: [%d], aRt9LID = 0x%x", status, lid);
  }
  return IMEConnect::mCurrentLID;
}

void copyShorttoUTF16(unsigned short *mCandidateWord, nsAString& aResult)
{
  int i;
  RT9_LOGD("Inside copyShorttoUTF16");

  for (i=0; i<IMEConnect::lenCandidateWord; i++) {
    aResult.Append((char16_t)mCandidateWord[i]);
  }
}

JSObject* IMEConnect::WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto)
{
  return mozilla::dom::IMEConnectBinding::Wrap(aCx, this, aGivenProto);
}

IMEConnect::~IMEConnect()
{
  RT9_LOGD("Inside IMEConnect destructor");
}
} // namespace dom
} // namespace mozilla
