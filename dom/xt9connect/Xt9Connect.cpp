/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Xt9Connect.h"
#include "mozilla/dom/Xt9ConnectBinding.h"

namespace mozilla {
namespace dom {

static demoIMEInfo         sIME;
static demoEditorInfo      sEditor;

bool      Xt9Connect::mEmptyWord;
nsCString Xt9Connect::mWholeWord;
nsCString Xt9Connect::sWholeWord;
nsCString Xt9Connect::mCandidateWord;
uint16_t  Xt9Connect::mTotalWord;
uint32_t  Xt9Connect::mCursorPosition;
uint32_t  Xt9Connect::sCursorPosition;
bool      Xt9Connect::cursorPositionEnabled;

uint32_t GetTickCount() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL)!= 0)
    return 0;
  return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void
EditorInitEmptyWord(demoIMEInfo * const pIME, bool& initEmptyWord)
{
  demoEditorInfo *pEditor = pIME->pEditor;
  if (initEmptyWord) {
    nsCString emptyWord("");
    pEditor->snCursorPos = pEditor->snBufferLen = 0;
    initEmptyWord = false;
  } else {
    XT9_LOGD("EditorInitEmptyWord::Xt9Connect::mEmptyWord: False");
  }
}

void
EditorInitWord(demoIMEInfo * const pIME, nsCString& initWord)
{
  demoEditorInfo *pEditor = pIME->pEditor;
  if (initWord.IsEmpty()) {
    XT9_LOGD("EditorInitWord::Xt9Connect::sWholeWord: Empty");
  } else {
    ET9INT initWordLength = initWord.Length();
    std::copy(initWord.get(),
      initWord.get() + initWordLength,
      pEditor->psBuffer);
    pEditor->snCursorPos = pEditor->snBufferLen = initWordLength;
    initWord.Assign("");
  }
}

void
EditorSetCursor(demoIMEInfo * const pIME,
                const uint32_t& initCursor,
                bool& enabledCursor)
{
  if (enabledCursor) {
    enabledCursor = false;
    demoEditorInfo *pEditor = pIME->pEditor;
    if (initCursor <= pEditor->snBufferLen) {
      pEditor->snCursorPos = initCursor;
    } else {
      XT9_LOGW("Cursor position is more than buffer length - exiting...");
    }
  } else {
    XT9_LOGD("EditorSetCursor::enabledCursor False");
  }
}

void
EditorInsertWord(demoIMEInfo * const pIME,
                 ET9AWWordInfo * pWord,
                 ET9BOOL bSupressSubstitutions)
{
  demoEditorInfo * const pEditor = pIME->pEditor;

  ET9STATUS eStatus;
  ET9U16 snStringLen;
  ET9SYMB *psString;

  MOZ_ASSERT(pEditor->snBufferLen <= BUFFER_LEN_MAX);
  MOZ_ASSERT(pEditor->snCursorPos <= pEditor->snBufferLen);

  if (pWord->wSubstitutionLen && !bSupressSubstitutions) {
    psString = pWord->sSubstitution;
    snStringLen = pWord->wSubstitutionLen;
  } else {
    psString = pWord->sWord;
    snStringLen = pWord->wWordLen;
  }

  if (snStringLen > (BUFFER_LEN_MAX - pEditor->snBufferLen)) {
    const ET9INT snDiscard =
    snStringLen - (BUFFER_LEN_MAX - pEditor->snBufferLen);

    if ((ET9U32)pEditor->snCursorPos > (BUFFER_LEN_MAX / 2)) {
      std::copy(pEditor->psBuffer+snDiscard,
                pEditor->psBuffer+pEditor->snBufferLen,
                pEditor->psBuffer);

      if (pEditor->snCursorPos >= snDiscard) {
        pEditor->snCursorPos -= snDiscard;
      } else {
        pEditor->snCursorPos = 0;
      }
    }

    pEditor->snBufferLen -= snDiscard;
  }

  std::copy_backward(pEditor->psBuffer+pEditor->snCursorPos,
                     pEditor->psBuffer+pEditor->snBufferLen,
                     pEditor->psBuffer+(pEditor->snBufferLen+snStringLen));

  std::copy(psString,
            psString + snStringLen,
            pEditor->psBuffer+pEditor->snCursorPos);

  pEditor->snBufferLen += snStringLen;

  eStatus = ET9AWNoteWordChanged(&pIME->sLingInfo,
                                 pEditor->psBuffer,
                                 pEditor->snBufferLen,
                                 (ET9U32)pEditor->snCursorPos,
                                 snStringLen,
                                 psEmptyString,
                                 NULL,
                                 0);

  MOZ_ASSERT(!eStatus || eStatus == ET9STATUS_INVALID_TEXT);

  pEditor->snCursorPos += snStringLen;
}

void
EditorGetWord(demoIMEInfo * const pIME,
              ET9SimpleWord * const pWord,
              const ET9BOOL bCut)
{
  demoEditorInfo * const pEditor = pIME->pEditor;

  ET9STATUS eStatus;
  ET9INT snStartPos;
  ET9INT snLastPos;

  if (!pEditor->snBufferLen) {
    pWord->wLen = 0;
    return;
  }

  snLastPos = pEditor->snCursorPos;

  if (snLastPos > 0 && iswspace(pEditor->psBuffer[snLastPos-1])) {
    if (iswspace(pEditor->psBuffer[snLastPos])
        || snLastPos == pEditor->snBufferLen) {
      pWord->wLen = 0;
      return;
    }
  }

  if (snLastPos == pEditor->snBufferLen
    || iswspace(pEditor->psBuffer[snLastPos])) {
    --snLastPos;
  }

  while (snLastPos+1 < pEditor->snBufferLen
    && !iswspace(pEditor->psBuffer[snLastPos+1])) {
    ++snLastPos;
  }

  snStartPos = snLastPos;

  while (snStartPos
    && !iswspace(pEditor->psBuffer[snStartPos - 1])
    && snLastPos - snStartPos < ET9MAXWORDSIZE) {
    --snStartPos;
  }

  pWord->wLen = (ET9U16)(snLastPos - snStartPos + 1);

  std::copy(pEditor->psBuffer+snStartPos,
            pEditor->psBuffer+snLastPos+1,
            pWord->sString);
  pWord->wCompLen = 0;

  if (!bCut) {
      return;
  }

  /* update model for removed word (if it wasn't cut the info
  could have been saved and used when replacing the word) */
  eStatus = ET9AWNoteWordChanged(&pIME->sLingInfo,
                                 pEditor->psBuffer,
                                 pEditor->snBufferLen,
                                 snStartPos,
                                 pWord->wLen,
                                 NULL,
                                 psEmptyString,
                                 0);

  MOZ_ASSERT(!eStatus || eStatus == ET9STATUS_INVALID_TEXT);

  /* cut from buffer */
  std::copy(pEditor->psBuffer+snLastPos+1,
            pEditor->psBuffer+pEditor->snBufferLen,
            pEditor->psBuffer+snStartPos);

  pEditor->snBufferLen -= pWord->wLen;
  pEditor->snCursorPos = snStartPos;
}

void
EditorDeleteChar(demoIMEInfo * const pIME)
{
  demoEditorInfo * const pEditor = pIME->pEditor;

  if (!pEditor->snCursorPos) {
    return;
  }

  if (pEditor->snCursorPos < pEditor->snBufferLen) {
    std::copy(pEditor->psBuffer+pEditor->snCursorPos,
              pEditor->psBuffer+pEditor->snBufferLen,
              pEditor->psBuffer+(pEditor->snCursorPos-1));
  }

  --pEditor->snCursorPos;
  --pEditor->snBufferLen;
}

void
EditorMoveBackward(demoIMEInfo * const pIME)
{
  demoEditorInfo * const pEditor = pIME->pEditor;

  if (pEditor->snCursorPos > 0) {
    --pEditor->snCursorPos;
  }
}

void
EditorMoveForward(demoIMEInfo * const pIME)
{
  demoEditorInfo * const pEditor = pIME->pEditor;

  if (pEditor->snCursorPos < pEditor->snBufferLen) {
    ++pEditor->snCursorPos;
  }
}

void
AcceptActiveWord(demoIMEInfo * const pIME, const ET9BOOL bAddSpace)
{
  if (pIME->bTotWords) {
    ET9AWWordInfo *pWord;

    /* use */
    ET9AWSelLstSelWord(&pIME->sLingInfo, pIME->bActiveWordIndex, 0);
    ET9AWSelLstGetWord(&pIME->sLingInfo, &pWord, pIME->bActiveWordIndex);

    EditorInsertWord(pIME, pWord, pIME->bSupressSubstitutions);
  }

  if (bAddSpace) {
    ET9AWWordInfo sSpace;

    sSpace.sWord[0] = ' ';
    sSpace.wWordLen = 1;
    sSpace.wWordCompLen = 0;
    sSpace.wSubstitutionLen = 0;

    EditorInsertWord(pIME, &sSpace, 0);
  }

  ET9ClearAllSymbs(&pIME->sWordSymbInfo);
}

ET9STATUS ET9FARCALL
ET9KDBLoad(ET9KDBInfo*   const pKdbInfo,
           const ET9U32  dwKdbNum,
           const ET9U16  wPageNum)
{
  ET9UINT nErrorLine;

  switch (dwKdbNum) {
    case (6 * 256) +  9 : /* English */
      return ET9KDB_Load_XmlKDB(pKdbInfo, ET9XmlLayoutWidth, ET9XmlLayoutHeight, wPageNum,
          l0609b00, 3018, ET9_XML_KEYBOARD_LAYOUT, NULL, &nErrorLine);
    case (6 * 256) + 10 : /* Spanish */
      return ET9KDB_Load_XmlKDB(pKdbInfo, ET9XmlLayoutWidth, ET9XmlLayoutHeight, wPageNum,
          l0610b00, 3095, ET9_XML_KEYBOARD_LAYOUT, NULL, &nErrorLine);
    case (6 * 256) + 12 : /* French */
      return ET9KDB_Load_XmlKDB(pKdbInfo, ET9XmlLayoutWidth, ET9XmlLayoutHeight, wPageNum,
          l0612b00, 3172, ET9_XML_KEYBOARD_LAYOUT, NULL, &nErrorLine);
    case (9 * 256) + 255 : /* HQR */
      return ET9KDB_Load_XmlKDB(pKdbInfo, ET9XmlLayoutWidth, ET9XmlLayoutHeight, wPageNum,
        l09255b00, 1685, NULL, NULL, &nErrorLine);
    default :
      return ET9STATUS_READ_DB_FAIL;
  }
}

ET9STATUS ET9FARCALL
ET9AWLdbReadData(ET9AWLingInfo *pLingInfo,
                 ET9U8         * ET9FARDATA *ppbSrc,
                 ET9U32        *pdwSizeInBytes)
{
  switch (pLingInfo->pLingCmnInfo->dwLdbNum) {
    case (11 * 256) + 9 : /* English */
      *ppbSrc = (ET9U8 ET9FARDATA *)l1109b00; *pdwSizeInBytes = 326771;
      return ET9STATUS_NONE;
    case (8 * 256) + 10 : /* Spanish */
      *ppbSrc = (ET9U8 ET9FARDATA *)l0810b00; *pdwSizeInBytes = 349415;
      return ET9STATUS_NONE;
    case (12 * 256) + 12 : /* French */
      *ppbSrc = (ET9U8 ET9FARDATA *)l1212b00; *pdwSizeInBytes = 348099;
      return ET9STATUS_NONE;
  default :
    XT9_LOGD("ET9AWLdbReadData = 0x%x", pLingInfo->pLingCmnInfo->dwLdbNum);
    return ET9STATUS_READ_DB_FAIL;
  }
}

ET9STATUS
ET9Handle_KDB_Request(ET9KDBInfo      * const pKdbInfo,
                      ET9WordSymbInfo * const pWordSymbInfo,
                      ET9KDB_Request  * const pRequest)
{
  demoIMEInfo * const pIME = (demoIMEInfo*)pKdbInfo->pPublicExtension;

  MOZ_ASSERT(pIME != NULL);

  switch (pRequest->eType) {
    case ET9_KDB_REQ_NONE :
      break;
    case ET9_KDB_REQ_TIMEOUT :
      pIME->dwMultitapStartTime = GetTickCount();
      break;
    default:
      break;
  }

  return ET9STATUS_NONE;
}

ET9STATUS
ET9Handle_DLM_Request(void * const pHandlerInfo,
                      ET9AWDLM_RequestInfo    * const pRequest)
{
  demoIMEInfo * const pIME = (demoIMEInfo*)pHandlerInfo;

  switch (pRequest->eType) {
    case ET9AW_DLM_Request_ExplicitLearningApproval:
      pIME->sExplicitLearningWord = pRequest->data.explicitLearningApproval.sWord;
      pRequest->data.explicitLearningApproval.bDidApprove = 0;
      break;
    case ET9AW_DLM_Request_ExplicitLearningExpired:
      pIME->sExplicitLearningWord.wLen = 0;
      break;
    default:
      break;
  }

  return ET9STATUS_NONE;
}

ET9U8
CharLookup(demoIMEInfo* const pIME,
           const int nChar)
{
  if (nChar >= 0x21 && nChar <= 0x7E) { /* if a valid character in DOS */
    if (nChar >= 0x61 && nChar <= 0x7A) { /* if an alphabetic character in DOS */
      if (pIME->eInputMode == I_HQR || pIME->eInputMode == I_HQD) {
        return pcMapKeyHQR[nChar - 0x61];
      } else {
        return 0x80;  /* add character explicitly */
      }
    } else if (nChar >= 0x30 && nChar <= 0x39 && pIME->eInputMode == I_HPD) {
      return pcMapKeyHPD[nChar - 0x30];
    } else if (nChar == 0x2c && pIME->eInputMode == I_HQR) {
      return 0;
    } else if (nChar == 0x2e && pIME->eInputMode == I_HQR) {
      return 1;
    } else {
      return 0x80; /* add character explicitly */
    }
  } else {
    return 0xFF; /* not a valid character */
  }
}

void
PrintLanguage(const ET9U32 dwLanguage)
{
  switch (dwLanguage & ET9PLIDMASK) {
    case ET9PLIDEnglish:
      XT9_LOGD("En");
      break;
    case ET9PLIDNull:
      XT9_LOGD("Gen");
      break;
    case ET9PLIDNone:
      XT9_LOGD("--");
      break;
    default:
      break;
  }
}

void
PrintState(demoIMEInfo *pIME)
{
  ET9U32 dwPrimaryLdbNum = 0;
  ET9POSTSHIFTMODE ePostShiftMode;

  ET9AWGetPostShiftMode(&pIME->sLingInfo, &ePostShiftMode);

  switch (ePostShiftMode) {
    case ET9POSTSHIFTMODE_LOWER:
      XT9_LOGD(" lower");
      break;
    case ET9POSTSHIFTMODE_INITIAL:
      XT9_LOGD(" initial");
      break;
    case ET9POSTSHIFTMODE_UPPER:
      XT9_LOGD(" upper");
      break;
    default:
    case ET9POSTSHIFTMODE_DEFAULT:
      if (ET9SHIFT_MODE(pIME->sKdbInfo.dwStateBits)) {
        XT9_LOGD(" Aa");
      } else if (ET9CAPS_MODE(pIME->sKdbInfo.dwStateBits)) {
        XT9_LOGD(" A ");
      } else {
        XT9_LOGD(" a ");
      }
      break;
  }

  XT9_LOGD(" SPC:");

  switch (ET9AW_GetSpellCorrectionMode(&pIME->sLingInfo)) {
    case ET9ASPCMODE_EXACT:
      XT9_LOGD("Exact");
      break;
    case ET9ASPCMODE_REGIONAL:
      XT9_LOGD("Reg");
      break;
    default:
      XT9_LOGD("Off");
      break;
  }

/*
  if (ET9NEXTLOCKING_MODE(&pIME->sWordSymbInfo)) {
      XT9_LOGD(" NextLock");
  }
*/

  switch (ET9AW_GetSelectionListMode(&pIME->sLingInfo)) {
    case ET9ASLCORRECTIONMODE_LOW:
      XT9_LOGD(" NextLock");
      break;
    case ET9ASLCORRECTIONMODE_HIGH:
      XT9_LOGD(" NextLock");
      break;
  }

  switch (pIME->eInputMode) {
    case I_HQR:
      XT9_LOGD(" HQR");
      break;
    case I_HPD:
      XT9_LOGD(" HPD");
      break;
    case I_HQD:
      XT9_LOGD(" HQD");
      break;
    case I_EXP:
      XT9_LOGD(" EXP");
      break;
    default:
      XT9_LOGD(" \?\?\?");
      break;
  }
}

void
PrintExplicitLearning(demoIMEInfo *pIME)
{
  ET9BOOL bUserAction;
  ET9BOOL bScanAction;
  ET9BOOL bAskForLanguageDiff;

  if (ET9AWGetExplicitLearning(&pIME->sLingInfo, &bUserAction, &bScanAction,
                               &bAskForLanguageDiff) || !bUserAction) {
    return;
  }

  if (!pIME->sExplicitLearningWord.wLen) {
    return;
  }

  XT9_LOGD("   Ctrl-X - Learn word: ");

  ET9SimpleWord * const pWord = &pIME->sExplicitLearningWord;
  ET9U16 wIndex;

  for (wIndex = 0; wIndex < pWord->wLen; ++wIndex) {
    XT9_LOGD("%c", pWord->sString[wIndex]);
  }
}

void
PrintCandidateList(demoIMEInfo *pIME)
{
  ET9STATUS       eStatus;
  ET9U16          wIndex;
  ET9U8           bCandidateIndex;

  if (!pIME->bTotWords) {
    return;
  }

  Xt9Connect::mTotalWord = pIME->bTotWords;

  for (bCandidateIndex = (pIME->bActiveWordIndex/5)*5;
       bCandidateIndex < (pIME->bActiveWordIndex/5)*5 + 5 && bCandidateIndex < pIME->bTotWords;
       ++bCandidateIndex) {
    ET9AWWordInfo *pWord;

    eStatus = ET9AWSelLstGetWord(&pIME->sLingInfo, &pWord, bCandidateIndex);

    if (eStatus != ET9STATUS_NONE) {
      MOZ_ASSERT(0);
      continue;
    }

    nsCString dbgWord;
    nsCString jsWord;

    /* print prefix */
    dbgWord.Append("Word ");
    dbgWord.AppendInt(bCandidateIndex);
    dbgWord.Append(" ");
    dbgWord.Append((pWord->bIsSpellCorr ? 'C' : ' '));

    /* print equality with active word marker (if applicable) */
    dbgWord.Append((bCandidateIndex == pIME->bActiveWordIndex ? "=>" : "= "));

    jsWord.Append((bCandidateIndex == pIME->bActiveWordIndex ? ">" : ""));

    /* print candidate */
    for (wIndex = 0; wIndex < pWord->wWordLen; ++wIndex) {
      if (wIndex == (pWord->wWordLen - pWord->wWordCompLen)) {
        dbgWord.Append("[");
      }

      dbgWord.Append(pWord->sWord[wIndex]);

      jsWord.Append(pWord->sWord[wIndex]);
    }

    if (pWord->wWordCompLen) {
      dbgWord.Append("]");
    }

    jsWord.Append(" ");

    XT9_LOGD("%s", dbgWord.get());
    Xt9Connect::mCandidateWord.Append(jsWord);

    /* print auto subst */
    if (pWord->wSubstitutionLen && !pIME->bSupressSubstitutions) {
      XT9_LOGD(" -> ");
      for (wIndex = 0; wIndex < pWord->wSubstitutionLen; ++wIndex) {
        XT9_LOGD("%c", pWord->sSubstitution[wIndex]);
      }
    }
  }
}

void
PrintEditorBuffer(demoEditorInfo *pEditor)
{
  ET9U16    wIndex;

  nsCString dbgWord;
  nsCString jsWord;

  if (!pEditor->snBufferLen) {
    return;
  }

  if (!pEditor->snCursorPos) {
    dbgWord.Append("|");
  }

  for (wIndex = 0; wIndex < pEditor->snBufferLen; ++wIndex) {
    jsWord.Append(pEditor->psBuffer[wIndex]);
    dbgWord.Append(pEditor->psBuffer[wIndex]);

    if (wIndex + 1 == pEditor->snCursorPos) {
      dbgWord.Append("|");
    }
  }

  XT9_LOGD("%s", dbgWord.get());

  Xt9Connect::mWholeWord.Append(jsWord);

  Xt9Connect::mCursorPosition = pEditor->snCursorPos;
}

void
PrintScreen(demoIMEInfo *pIME)
{
  demoEditorInfo * const pEditor = pIME->pEditor;

  XT9_LOGD("\nCommands:\n");

  if (pIME->bTotWords) {
    XT9_LOGD("   Esc - Quit          Up Arrow - Prev Word    Down Arrow - Next Word \n");
  } else if (pEditor->snBufferLen) {
    XT9_LOGD("   Esc - Quit          Left Arrow - Cursor     Right Arrow - Cursor   \n");
  } else {
    XT9_LOGD("   Esc - Quit                                                        \n");
  }

  PrintExplicitLearning(pIME);

  PrintState(pIME);

  if (pIME->bTotWords) {
    PrintCandidateList(pIME);
  } else {
    PrintEditorBuffer(pEditor);
    XT9_LOGD("Enter: ");
  }
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(Xt9Connect, mWindow)
NS_IMPL_CYCLE_COLLECTING_ADDREF(Xt9Connect)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Xt9Connect)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Xt9Connect)
NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

Xt9Connect::Xt9Connect(nsPIDOMWindow* aWindow): mWindow(aWindow)
{
    MOZ_ASSERT(mWindow);
}

already_AddRefed<Xt9Connect>
Xt9Connect::Constructor(const GlobalObject& aGlobal,
                        ErrorResult& aRv)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsRefPtr<Xt9Connect> xt_object = new Xt9Connect(window);
  aRv = xt_object->Init();

  return xt_object.forget();
}

nsresult
Xt9Connect::Init()
{
  ET9STATUS LdbValidate_eStatus;
  ET9STATUS Validate_HPD_eStatus;

  memset(&sIME, 0, sizeof(demoIMEInfo));
  memset(&sEditor, 0, sizeof(demoEditorInfo));

  sIME.pEditor = &sEditor;

  ET9STATUS SymbInit_eStatus(ET9WordSymbInit(&sIME.sWordSymbInfo, 1));

  if (SymbInit_eStatus) {
    XT9_LOGE("Init::SymbInit_eStatus: [%d]", SymbInit_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS SysInit_eStatus(ET9AWSysInit(&sIME.sLingInfo, &sIME.sLingCmnInfo, &sIME.sWordSymbInfo, 1, SEL_LIST_SIZE, NULL));

  if (SysInit_eStatus) {
    XT9_LOGE("Init::SysInit_eStatus: [%d]", SysInit_eStatus);
    return NS_ERROR_FAILURE;
  }

  LdbValidate_eStatus = ET9AWLdbValidate(&sIME.sLingInfo, ET9LIDEnglish_US, &ET9AWLdbReadData);

  if (LdbValidate_eStatus) {
    XT9_LOGE("Init::LdbValidate_eStatus: [0x%x] [%d]", ET9LIDEnglish_US, LdbValidate_eStatus);
    return NS_ERROR_FAILURE;
  }

  LdbValidate_eStatus = ET9AWLdbValidate(&sIME.sLingInfo, ET9LIDFrench_Canada, &ET9AWLdbReadData);

  if (LdbValidate_eStatus) {
    XT9_LOGE("Init::LdbValidate_eStatus: [0x%x] [%d]", ET9LIDFrench_Canada, LdbValidate_eStatus);
    return NS_ERROR_FAILURE;
  }

  LdbValidate_eStatus = ET9AWLdbValidate(&sIME.sLingInfo, ET9LIDSpanish_LatinAmerican, &ET9AWLdbReadData);

  if (LdbValidate_eStatus) {
    XT9_LOGE("Init::LdbValidate_eStatus: [0x%x] [%d]", ET9LIDSpanish_LatinAmerican, LdbValidate_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS LdbInit_eStatus(ET9AWLdbInit(&sIME.sLingInfo, &ET9AWLdbReadData));

  if (LdbInit_eStatus) {
    XT9_LOGE("Init::LdbInit_eStatus: [%d]", LdbInit_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS LdbSetLanguage_eStatus(ET9AWLdbSetLanguage(&sIME.sLingInfo, ET9LIDEnglish_US, ET9PLIDNone, 1));

  if (LdbSetLanguage_eStatus) {
    XT9_LOGE("Init::LdbSetLanguage_eStatus: [%d]", LdbSetLanguage_eStatus);
    return NS_ERROR_FAILURE;
  }

  if (sIME.pDLMInfo) {
    free(sIME.pDLMInfo);
    sIME.pDLMInfo = NULL;
  }

  sIME.dwDLMSize = ET9AWDLM_SIZE_NORMAL;

  sIME.pDLMInfo = (_ET9DLM_info*)malloc(sIME.dwDLMSize);

  if (!sIME.pDLMInfo) {
    return NS_ERROR_FAILURE;
  }

  memset(sIME.pDLMInfo, 0, sIME.dwDLMSize);

  ET9STATUS AWDLMInit_eStatus(ET9AWDLMInit(&sIME.sLingInfo, sIME.pDLMInfo, sIME.dwDLMSize, 0));

  if (AWDLMInit_eStatus) {
    XT9_LOGE("Init::AWDLMInit_eStatus: [%d]", AWDLMInit_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS AWDLMRegister_eStatus(ET9AWDLMRegisterForRequests(&sIME.sLingInfo, ET9Handle_DLM_Request, &sIME));

  if (AWDLMRegister_eStatus) {
    XT9_LOGE("Init::AWDLMRegister_eStatus: [%d]", AWDLMRegister_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS AWASDBInit_eStatus(ET9AWASDBInit(&sIME.sLingInfo, (ET9AWASDBInfo*)sIME.pbASdb, ASDB_SIZE, NULL));

  if (AWASDBInit_eStatus) {
    XT9_LOGE("Init::AWASDBInit_eStatus: [%d]", AWASDBInit_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS AWClearNextWord_eStatus(ET9AWClearNextWordPrediction(&sIME.sLingInfo));

  if (AWClearNextWord_eStatus) {
    XT9_LOGE("Init::AWClearNextWord_eStatus: [%d]", AWClearNextWord_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS AWSetWordCompl_eStatus(ET9AWSetWordCompletionPoint(&sIME.sLingInfo, 4));

  if (AWSetWordCompl_eStatus) {
    XT9_LOGE("Init::AWSetWordCompl_eStatus: [%d]", AWSetWordCompl_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS AWSetSpell_eStatus(ET9AWSetSpellCorrectionMode(&sIME.sLingInfo, ET9ASPCMODE_REGIONAL, 0));

  if (AWSetSpell_eStatus) {
    XT9_LOGE("Init::AWSetSpell_eStatus: [%d]", AWSetSpell_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS Init_eStatus(ET9KDB_Init(&sIME.sKdbInfo, &sIME.sWordSymbInfo,
                                     GENERIC_HQR, 0, 0, 0, ET9KDBLoad, &ET9Handle_KDB_Request, &sIME));

  if (Init_eStatus) {
    XT9_LOGE("Init::Init_eStatus: [%d]", Init_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS Validate_eStatus(ET9KDB_Validate(&sIME.sKdbInfo, GENERIC_HQR, ET9KDBLoad));

  if (Validate_eStatus) {
    XT9_LOGE("Init::Validate_eStatus: [%d]", Validate_eStatus);
    return NS_ERROR_FAILURE;
  }

  Validate_HPD_eStatus = ET9KDB_Validate(&sIME.sKdbInfo, ENGLISH_HPD, ET9KDBLoad);

  if (Validate_HPD_eStatus) {
    XT9_LOGE("Init::Validate_HPD_eStatus: [0x%x] [%d]", ENGLISH_HPD, Validate_HPD_eStatus);
    return NS_ERROR_FAILURE;
  }

  Validate_HPD_eStatus = ET9KDB_Validate(&sIME.sKdbInfo, FRENCH_HPD, ET9KDBLoad);

  if (Validate_HPD_eStatus) {
    XT9_LOGE("Init::Validate_HPD_eStatus: [0x%x] [%d]", FRENCH_HPD, Validate_HPD_eStatus);
    return NS_ERROR_FAILURE;
  }

  Validate_HPD_eStatus = ET9KDB_Validate(&sIME.sKdbInfo, SPANISH_HPD, ET9KDBLoad);

  if (Validate_HPD_eStatus) {
    XT9_LOGE("Init::Validate_HPD_eStatus: [0x%x] [%d]", SPANISH_HPD, Validate_HPD_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9U16  wPageNum;
  ET9U16  wSecondPageNum;
  ET9KDB_GetPageNum(&sIME.sKdbInfo, &wPageNum, &wSecondPageNum);
  sIME.eInputMode = I_HPD;
  ET9KDB_SetKdbNum(&sIME.sKdbInfo, ((sIME.sLingCmnInfo.dwFirstLdbNum & ET9PLIDMASK) | ET9SKIDPhonePad),
                   wPageNum, ((sIME.sLingCmnInfo.dwSecondLdbNum & ET9PLIDMASK) | ET9SKIDPhonePad),
                   wSecondPageNum);

  ET9STATUS SetRegionalMode_eStatus(ET9KDB_SetRegionalMode(&sIME.sKdbInfo));

  if (SetRegionalMode_eStatus) {
    XT9_LOGE("Init::SetRegionalMode_eStatus: [%d]", SetRegionalMode_eStatus);
    return NS_ERROR_FAILURE;
  }

  ET9STATUS SetAmbigMode_eStatus(ET9KDB_SetAmbigMode(&sIME.sKdbInfo, 0, 0));

  if (SetAmbigMode_eStatus) {
    XT9_LOGE("Init::SetAmbigMode_eStatus: [%d]", SetAmbigMode_eStatus);
    return NS_ERROR_FAILURE;
  }

  PrintScreen(&sIME);

  return NS_OK;
}

void
Xt9Connect::SetLetter(const unsigned long aHexPrefix, const unsigned long aHexLetter, ErrorResult& aRv)
{
  int nInputPrefix(aHexPrefix);
  XT9_LOGD("SetLetter::nInputPrefix: %x", nInputPrefix);

  int nInputChar(aHexLetter);
  XT9_LOGD("SetLetter::nInputChar: %x", nInputChar);

  if (nInputChar == 0x1B) {
      return;
  }

  EditorInitWord(&sIME, Xt9Connect::sWholeWord); // send init word

  EditorInitEmptyWord(&sIME, Xt9Connect::mEmptyWord); // send empty init word

  EditorSetCursor(&sIME, Xt9Connect::sCursorPosition, Xt9Connect::cursorPositionEnabled); // set cursor position

  if (nInputPrefix == 0xE0) {
    switch (nInputChar) {
      case 0x48: /* up arrow */
        if (sIME.bTotWords && sIME.bActiveWordIndex == 0) {
          sIME.bActiveWordIndex = (ET9U8)(sIME.bTotWords - 1);
        } else {
          --sIME.bActiveWordIndex;
        }
        break;
      case 0x50: /* down arrow */
        ++sIME.bActiveWordIndex;
        if (sIME.bTotWords && sIME.bActiveWordIndex == sIME.bTotWords) {
          sIME.bActiveWordIndex = 0;
        }
        break;
      case 0x4B: /* left arrow -  */
        if (!sIME.bTotWords) {
          EditorMoveBackward(&sIME);
        }
        break;
      case 0x4D: /* right arrow -  */
        if (!sIME.bTotWords) {
          EditorMoveForward(&sIME);
        }
        break;
      case 0x85: /* F11 - reselect */
      {
        ET9SimpleWord sString;

        EditorGetWord(&sIME, &sString, 1);

        if (sString.wLen) {
          ET9BOOL bSelectedWasAutomatic;
          ET9BOOL bWasFoundInHistory;

          ET9AWReselectWord(&sIME.sLingInfo, &sIME.sKdbInfo,
                            sString.sString, sString.wLen,
                            &sIME.bTotWords, &sIME.bActiveWordIndex,
                            &bSelectedWasAutomatic, &bWasFoundInHistory);

          if (!bWasFoundInHistory) {
            ET9AWSelLstBuild(&sIME.sLingInfo, &sIME.bTotWords,
                           &sIME.bActiveWordIndex, &sIME.wGestureValue);
          }
        }
      }
        break;
      default:
        break;
    }
  } else {
    switch (nInputChar) {
      case 0x08: /* delete */
      {
        ET9AWWordInfo *pWord;

        if (sIME.bTotWords && !sIME.sWordSymbInfo.wNumSymbs) {
          sIME.bTotWords = 0;
        } else if (!sIME.bSupressSubstitutions &&
                   sIME.bTotWords &&
                   !ET9AWSelLstGetWord(&sIME.sLingInfo, &pWord, sIME.bActiveWordIndex) &&
                   pWord->wSubstitutionLen) {
          sIME.bSupressSubstitutions = 1;
        } else if (sIME.bTotWords) {
          sIME.bSupressSubstitutions = 0;
          ET9ClearOneSymb(&sIME.sWordSymbInfo);

          ET9AWSelLstBuild(&sIME.sLingInfo, &sIME.bTotWords,
                           &sIME.bActiveWordIndex, &sIME.wGestureValue);
        } else {
          EditorDeleteChar(&sIME);
        }
      }
        break;
      case 0x20: /* space - flush */
      case 0x0D: /* enter - flush */
        AcceptActiveWord(&sIME, (nInputChar == 0x20) ? 1 : 0);

        ET9AWSelLstBuild(&sIME.sLingInfo, &sIME.bTotWords, &sIME.bActiveWordIndex, &sIME.wGestureValue);

        break;
      default:
          ET9U8 bKey(CharLookup(&sIME, nInputChar));

        if (bKey != 0xFF) {
          if (bKey == 0x80) {
            ET9INPUTSHIFTSTATE eShiftState;
            if (ET9SHIFT_MODE(sIME.sKdbInfo.dwStateBits)) {
              eShiftState = ET9SHIFT;
            } else if (ET9CAPS_MODE(sIME.sKdbInfo.dwStateBits)) {
              eShiftState = ET9CAPSLOCK;
            } else {
              eShiftState = ET9NOSHIFT;
            }
            ET9AddExplicitSymb(&sIME.sWordSymbInfo, (ET9SYMB)nInputChar, 0, eShiftState, sIME.bActiveWordIndex);
            if (eShiftState == ET9SHIFT) {
              ET9SetUnShift(&sIME.sWordSymbInfo);
            }
          } else {
            ET9SYMB sFunctionKey;

            if (ET9_KDB_MULTITAP_MODE(sIME.sKdbInfo.dwStateBits)) {
                if ((sIME.dwMultitapStartTime == 0) || (sIME.bLastKeyMT != bKey)) {
                    sIME.bLastKeyMT = bKey;
                } else if ((GetTickCount() - sIME.dwMultitapStartTime) >= MT_TIMEOUT_TIME) {
                    ET9KDB_TimeOut(&sIME.sKdbInfo);
                }
            }

            ET9KDB_ProcessKey(&sIME.sKdbInfo, bKey, 0, sIME.bActiveWordIndex, &sFunctionKey);
          }

          ET9AWSelLstBuild(&sIME.sLingInfo, &sIME.bTotWords, &sIME.bActiveWordIndex, &sIME.wGestureValue);
        }

        sIME.bSupressSubstitutions = 0;

        break;
    }
  }
  PrintScreen(&sIME);
}

JSObject*
Xt9Connect::WrapObject(JSContext* aCx)
{
  return mozilla::dom::Xt9ConnectBinding::Wrap(aCx, this);
}

Xt9Connect::~Xt9Connect()
{
  if (sIME.pDLMInfo) {
    free(sIME.pDLMInfo);
    sIME.pDLMInfo = NULL;
  }
}
} // namespace dom
} // namespace mozilla