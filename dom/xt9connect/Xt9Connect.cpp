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
uint32_t  Xt9Connect::mCursorPostion;

uint32_t GetTickCount()
{
	struct timeval tv;
	if(gettimeofday(&tv, NULL)!= 0)
		return 0;
	return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void EditorInitEmptyWord(demoIMEInfo *pIME, bool& initEmptyWord)
{
    demoEditorInfo *pEditor = pIME->pEditor;
    if (initEmptyWord)
    {
        nsCString emptyWord("");
        std::copy(emptyWord.get(), emptyWord.get(), pEditor->psBuffer);
        pEditor->snCursorPos = pEditor->snBufferLen = 0;
        initEmptyWord = false;
    } else {
        LOG_DBG("EditorInitEmptyWord::Xt9Connect::mEmptyWord: False");
    }
}

void EditorInitWord(demoIMEInfo *pIME, nsCString& initWord)
{
    demoEditorInfo *pEditor = pIME->pEditor;
    if (initWord.IsEmpty()) {
        LOG_DBG("EditorInitWord::Xt9Connect::sWholeWord: Empty");
    } else {
        ET9INT initWordLength = initWord.Length();
        std::copy(initWord.get(), initWord.get() + initWordLength, pEditor->psBuffer);
        pEditor->snCursorPos = pEditor->snBufferLen = initWordLength;
        initWord.Assign("");
        ET9CursorMoved(&pIME->sWordSymbInfo, 1);
    }
}

void EditorInsertWord(demoIMEInfo * const pIME, ET9AWWordInfo *pWord, ET9BOOL bSupressSubstitutions)
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
    }
    else {
        psString = pWord->sWord;
        snStringLen = pWord->wWordLen;
    }

    if (snStringLen > (BUFFER_LEN_MAX - pEditor->snBufferLen)) {

        const ET9INT snDiscard = snStringLen - (BUFFER_LEN_MAX - pEditor->snBufferLen);

        if ((ET9U32)pEditor->snCursorPos > (BUFFER_LEN_MAX / 2)) {

            std::copy(pEditor->psBuffer+snDiscard, pEditor->psBuffer+pEditor->snBufferLen, pEditor->psBuffer);

            if (pEditor->snCursorPos >= snDiscard) {
                pEditor->snCursorPos -= snDiscard;
            }
            else {
                pEditor->snCursorPos = 0;
            }
        }

        pEditor->snBufferLen -= snDiscard;
    }

    std::copy_backward(pEditor->psBuffer+pEditor->snCursorPos, pEditor->psBuffer+pEditor->snBufferLen, pEditor->psBuffer+(pEditor->snBufferLen+snStringLen));

    std::copy(psString, psString + snStringLen, pEditor->psBuffer+pEditor->snCursorPos);
    pEditor->snBufferLen += snStringLen;

    eStatus = ET9AWNoteWordChanged(&pIME->sLingInfo,
                                   pEditor->psBuffer,
                                   pEditor->snBufferLen,
                                   (ET9U32)pEditor->snCursorPos,
                                   snStringLen,
                                   psEmptyString,
                                   NULL,
                                   0);

    if (!eStatus || eStatus == ET9STATUS_INVALID_TEXT) {
    	MOZ_ASSERT(!eStatus || eStatus == ET9STATUS_INVALID_TEXT);
    }

    pEditor->snCursorPos += snStringLen;
}

void EditorGetWord(demoIMEInfo * const pIME, ET9SimpleWord * const pWord, const ET9BOOL bCut)
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
        if (iswspace(pEditor->psBuffer[snLastPos]) || snLastPos == pEditor->snBufferLen) {
            pWord->wLen = 0;
            return;
        }
    }

    if (snLastPos == pEditor->snBufferLen || iswspace(pEditor->psBuffer[snLastPos])) {
        --snLastPos;
    }

    while (snLastPos+1 < pEditor->snBufferLen && !iswspace(pEditor->psBuffer[snLastPos+1])) {
        ++snLastPos;
    }

    snStartPos = snLastPos;

    while (snStartPos && !iswspace(pEditor->psBuffer[snStartPos - 1]) && snLastPos - snStartPos < ET9MAXWORDSIZE) {
        --snStartPos;
    }

    pWord->wLen = (ET9U16)(snLastPos - snStartPos + 1);

    std::copy(pEditor->psBuffer+snStartPos, pEditor->psBuffer+snLastPos+1, pWord->sString);
    pWord->wCompLen = 0;

    if (!bCut) {
        return;
    }

    /* update model for removed word (if it wasn't cut the info could have been saved and used when replacing the word) */

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

    std::copy(pEditor->psBuffer+snLastPos+1, pEditor->psBuffer+pEditor->snBufferLen, pEditor->psBuffer+snStartPos);

    pEditor->snBufferLen -= pWord->wLen;
    pEditor->snCursorPos = snStartPos;
}

void EditorDeleteChar(demoIMEInfo * const pIME)
{
    demoEditorInfo * const pEditor = pIME->pEditor;

    if (!pEditor->snCursorPos) {
        return;
    }

    if (pEditor->snCursorPos < pEditor->snBufferLen) {
       std::copy(pEditor->psBuffer+pEditor->snCursorPos, pEditor->psBuffer+pEditor->snBufferLen, pEditor->psBuffer+(pEditor->snCursorPos-1));
    }

    --pEditor->snCursorPos;
    --pEditor->snBufferLen;

    ET9CursorMoved(&pIME->sWordSymbInfo, 1);
}

void EditorMoveBackward(demoIMEInfo * const pIME)
{
    demoEditorInfo * const pEditor = pIME->pEditor;

    if (pEditor->snCursorPos > 0) {
        --pEditor->snCursorPos;
        ET9CursorMoved(&pIME->sWordSymbInfo, 1);
    }
}

void EditorMoveForward(demoIMEInfo * const pIME)
{
    demoEditorInfo * const pEditor = pIME->pEditor;

    if (pEditor->snCursorPos < pEditor->snBufferLen) {
        ++pEditor->snCursorPos;
        ET9CursorMoved(&pIME->sWordSymbInfo, 1);
    }
}

void AcceptActiveWord(demoIMEInfo * const pIME, const ET9BOOL bAddSpace)
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

ET9STATUS ET9Handle_IMU_Request(ET9WordSymbInfo * const pWordSymbInfo,
                                ET9_Request     * const pRequest)
{
    demoIMEInfo * const pIME = (demoIMEInfo*)pWordSymbInfo->pPublicExtension;

    if (!pIME  || !pIME->pEditor) {
        return ET9STATUS_ERROR;
    }

    switch (pRequest->eType)
    {
        case ET9_REQ_AutoAccept:
            AcceptActiveWord(pIME, pRequest->data.sAutoAcceptInfo.bAddSpace);
            break;

        case ET9_REQ_AutoCap:
            {
                const ET9U32 dwContextLen = (ET9U32)pIME->pEditor->snCursorPos;
                const ET9U32 dwStartIndex = (dwContextLen <= pRequest->data.sAutoCapInfo.dwMaxBufLen) ? 0 :
                                            (dwContextLen - pRequest->data.sAutoCapInfo.dwMaxBufLen - 1);

                pRequest->data.sAutoCapInfo.dwBufLen = dwContextLen - dwStartIndex;

                std::copy(pIME->pEditor->psBuffer+dwStartIndex, pIME->pEditor->psBuffer+(dwStartIndex+pRequest->data.sAutoCapInfo.dwBufLen), pRequest->data.sAutoCapInfo.psBuf);
            }
            break;

        case ET9_REQ_BufferContext:
            {
                const ET9U32 dwContextLen = (ET9U32)pIME->pEditor->snCursorPos;
                const ET9U32 dwStartIndex = (dwContextLen <= pRequest->data.sBufferContextInfo.dwMaxBufLen) ? 0 :
                                            (dwContextLen - pRequest->data.sBufferContextInfo.dwMaxBufLen);

                pRequest->data.sBufferContextInfo.dwBufLen = dwContextLen - dwStartIndex;

                std::copy(pIME->pEditor->psBuffer+dwStartIndex, pIME->pEditor->psBuffer+(dwStartIndex+pRequest->data.sBufferContextInfo.dwBufLen), pRequest->data.sBufferContextInfo.psBuf);
            }
            break;

        default:
        	break;
    }

    return ET9STATUS_NONE;
}

ET9STATUS ET9FARCALL ET9KDBLoad(ET9KDBInfo    * const pKdbInfo,
                        	   const ET9U32          dwKdbNum,
                        	   const ET9U16          wPageNum)
{
	ET9UINT nErrorLine;

	switch (dwKdbNum) {

		case ( 6 * 256) +  9 : /* English */
		    return ET9KDB_Load_XmlKDB(pKdbInfo, ET9XmlLayoutWidth, ET9XmlLayoutHeight, wPageNum, 
		    						  l0609b00, 3018, NULL, NULL, &nErrorLine);

		case ( 9 * 256) + 255 : /* HQR */
		    return ET9KDB_Load_XmlKDB(pKdbInfo, ET9XmlLayoutWidth, ET9XmlLayoutHeight, wPageNum, 
		    						  l09255b00, 20492, NULL, NULL, &nErrorLine);

		default :
		    return ET9STATUS_READ_DB_FAIL;
	}
}

ET9STATUS ET9FARCALL ET9AWLdbReadData(ET9AWLingInfo *pLingInfo, 
                                      ET9U8         * ET9FARDATA *ppbSrc, 
                                      ET9U32        *pdwSizeInBytes)
{
        switch (pLingInfo->pLingCmnInfo->dwLdbNum) {

        case ( 1 * 256) +  9 : /* English */
            *ppbSrc = (ET9U8 ET9FARDATA *)l0109b00; *pdwSizeInBytes = 319969;
            return ET9STATUS_NONE;

        default :
            return ET9STATUS_READ_DB_FAIL;
        }
}

ET9STATUS ET9Handle_KDB_Request(ET9KDBInfo      * const pKdbInfo,
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

ET9STATUS ET9Handle_DLM_Request(void                    * const pHandlerInfo,
                                ET9AWDLM_RequestInfo    * const pRequest)
{
    demoIMEInfo * const pIME = (demoIMEInfo*)pHandlerInfo;

    switch (pRequest->eType)
    {
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

ET9U8 CharLookup(demoIMEInfo     * const pIME,
                const int               nChar)
{
    if (nChar >= 0x21 && nChar <= 0x7E) { /* if a valid character in DOS */

        if (nChar >= 0x61 && nChar <= 0x7A) { /* if an alphabetic character in DOS */

            if (pIME->eInputMode == I_HQR || pIME->eInputMode == I_HQD) {
                return pcMapKeyHQR[nChar - 0x61];
            }
            else {
                return 0x80;  /* add character explicitly */
            }
        }
        else if (nChar >= 0x30 && nChar <= 0x39 && pIME->eInputMode == I_HPD) {
            return pcMapKeyHPD[nChar - 0x30];
        }
        else if (nChar == 0x2c && pIME->eInputMode == I_HQR) {
            return 0;
        }
        else if (nChar == 0x2e && pIME->eInputMode == I_HQR) {
            return 1;
        }
        else {
            return 0x80; /* add character explicitly */
        }
    }
    else {
        return 0xFF; /* not a valid character */
    }
}

void PrintLanguage(const ET9U32 dwLanguage)
{
    switch (dwLanguage & ET9PLIDMASK)
    {
        case ET9PLIDEnglish:
        	LOG_DBG("En");
            break;
        case ET9PLIDNull:
        	LOG_DBG("Gen");
            break;
        case ET9PLIDNone:
        	LOG_DBG("--");
            break;
        default:
            break;
    }
}

void PrintState(demoIMEInfo *pIME)
{
    ET9U32 dwPrimaryLdbNum = 0;
    ET9POSTSHIFTMODE ePostShiftMode;

    ET9AWGetPostShiftMode(&pIME->sLingInfo, &ePostShiftMode);

    switch (ePostShiftMode) {
        case ET9POSTSHIFTMODE_LOWER:
            LOG_DBG(" lower");
            break;
        case ET9POSTSHIFTMODE_INITIAL:
            LOG_DBG(" initial");
            break;
        case ET9POSTSHIFTMODE_UPPER:
            LOG_DBG(" upper");
            break;
        default:
        case ET9POSTSHIFTMODE_DEFAULT:
            if (ET9SHIFT_MODE(&pIME->sWordSymbInfo)) {
                LOG_DBG(" Aa");
            }
            else if (ET9CAPS_MODE(&pIME->sWordSymbInfo)) {
                LOG_DBG(" A ");
            }
            else {
                LOG_DBG(" a ");
            }
            break;
    }

    LOG_DBG(" SPC:");

    switch (ET9AW_GetSpellCorrectionMode(&pIME->sLingInfo))
    {
        case ET9ASPCMODE_EXACT:
            LOG_DBG("Exact");
            break;
        case ET9ASPCMODE_REGIONAL:
            LOG_DBG("Reg");
            break;
        default:
            LOG_DBG("Off");
            break;
    }

    if (ET9NEXTLOCKING_MODE(&pIME->sWordSymbInfo)) {
        LOG_DBG(" NextLock");
    }

    switch (ET9AW_GetSelectionListMode(&pIME->sLingInfo))
    {
        case ET9ASLCORRECTIONMODE_LOW:
            LOG_DBG(" NextLock");
            break;
        case ET9ASLCORRECTIONMODE_HIGH:
            LOG_DBG(" NextLock");
            break;
    }

    switch (pIME->eInputMode)
    {
        case I_HQR:
            LOG_DBG(" HQR");
            break;
        case I_HPD:
            LOG_DBG(" HPD");
            break;
        case I_HQD:
            LOG_DBG(" HQD");
            break;
        case I_EXP:
            LOG_DBG(" EXP");
            break;
        default:
            LOG_DBG(" \?\?\?");
            break;
    }
}

void PrintExplicitLearning(demoIMEInfo *pIME)
{
    {
        ET9BOOL bUserAction;
        ET9BOOL bScanAction;
        ET9BOOL bAskForLanguageDiff;

        if (ET9AWGetExplicitLearning(&pIME->sLingInfo, &bUserAction, &bScanAction, &bAskForLanguageDiff) || !bUserAction) {
            return;
        }
    }

    if (!pIME->sExplicitLearningWord.wLen) {
        return;
    }

    LOG_DBG("   Ctrl-X - Learn word: ");

    {
        ET9SimpleWord * const pWord = &pIME->sExplicitLearningWord;

        ET9U16 wIndex;

        for (wIndex = 0; wIndex < pWord->wLen; ++wIndex) {
            LOG_DBG("%c", pWord->sString[wIndex]);
        }
    }

}

void PrintCandidateList(demoIMEInfo *pIME)
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

        LOG_DBG("%s", dbgWord.get());
        Xt9Connect::mCandidateWord.Append(jsWord);

        /* print auto subst */

        if (pWord->wSubstitutionLen && !pIME->bSupressSubstitutions) {
            LOG_DBG(" -> ");
            for (wIndex = 0; wIndex < pWord->wSubstitutionLen; ++wIndex) {
                LOG_DBG("%c", pWord->sSubstitution[wIndex]);
            }
        }
    }
}

void PrintEditorBuffer(demoEditorInfo *pEditor)
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

    LOG_DBG("%s", dbgWord.get());

    Xt9Connect::mWholeWord.Append(jsWord);

    Xt9Connect::mCursorPostion = pEditor->snCursorPos;
}

void PrintScreen(demoIMEInfo *pIME)
{
    demoEditorInfo * const pEditor = pIME->pEditor;

    LOG_DBG("\nCommands:\n");

    if (pIME->bTotWords) {
    LOG_DBG("   Esc - Quit          Up Arrow - Prev Word    Down Arrow - Next Word \n");
    }
    else if (pEditor->snBufferLen) {
    LOG_DBG("   Esc - Quit          Left Arrow - Cursor     Right Arrow - Cursor   \n");
    }
    else {
    LOG_DBG("   Esc - Quit                                                        \n");
    }

    PrintExplicitLearning(pIME);

    PrintState(pIME);

    if (pIME->bTotWords) {
        PrintCandidateList(pIME);
    }
    else {
        PrintEditorBuffer(pEditor);
        LOG_DBG("Enter: ");
    }
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(Xt9Connect, mWindow)
NS_IMPL_CYCLE_COLLECTING_ADDREF(Xt9Connect)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Xt9Connect)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Xt9Connect)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

Xt9Connect::Xt9Connect(nsPIDOMWindow* aWindow)
	: mWindow(aWindow)
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

	xt_object->Init(aRv);

	return xt_object.forget();
}

already_AddRefed<Xt9Connect>
Xt9Connect::Init(ErrorResult& aRv)
{

	memset(&sIME, 0, sizeof(demoIMEInfo));
	memset(&sEditor, 0, sizeof(demoEditorInfo));

	sIME.pEditor = &sEditor;

	ET9STATUS SymbInit_eStatus(ET9WordSymbInit(&sIME.sWordSymbInfo, 1, ET9Handle_IMU_Request, &sIME));

    if (SymbInit_eStatus) {
        LOG_DBG("Init::SymbInit_eStatus: [%d]", SymbInit_eStatus);
        return nullptr;
    }

	ET9STATUS SysInit_eStatus(ET9AWSysInit(&sIME.sLingInfo, &sIME.sLingCmnInfo, &sIME.sWordSymbInfo, 1, SEL_LIST_SIZE, NULL));

    if (SysInit_eStatus) {
        LOG_DBG("Init::SysInit_eStatus: [%d]", SysInit_eStatus);
        return nullptr;
    }

	ET9STATUS LdbValidate_eStatus(ET9AWLdbValidate(&sIME.sLingInfo, ET9PLIDEnglish, &ET9AWLdbReadData));

    if (LdbValidate_eStatus) {
        LOG_DBG("Init::LdbValidate_eStatus: [%d]", LdbValidate_eStatus);
        return nullptr;
    }

	ET9STATUS LdbInit_eStatus(ET9AWLdbInit(&sIME.sLingInfo, &ET9AWLdbReadData));

    if (LdbInit_eStatus) {
        LOG_DBG("Init::LdbInit_eStatus: [%d]", LdbInit_eStatus);
        return nullptr;
    }

	ET9STATUS LdbSetLanguage_eStatus(ET9AWLdbSetLanguage(&sIME.sLingInfo, ET9PLIDEnglish, ET9PLIDNone, 1));

    if (LdbSetLanguage_eStatus) {
        LOG_DBG("Init::LdbSetLanguage_eStatus: [%d]", LdbSetLanguage_eStatus);
        return nullptr;
    }

    if (sIME.pDLMInfo) {
        free(sIME.pDLMInfo);
        sIME.pDLMInfo = NULL;
    }

	sIME.dwDLMSize = ET9AWDLM_SIZE_NORMAL;

    sIME.pDLMInfo = (_ET9DLM_info*)malloc(sIME.dwDLMSize);

    if (!sIME.pDLMInfo) {
        aRv.Throw(NS_ERROR_UNEXPECTED);
        return nullptr;
    }

    memset(sIME.pDLMInfo, 0, sIME.dwDLMSize);

    ET9STATUS AWDLMInit_eStatus(ET9AWDLMInit(&sIME.sLingInfo, sIME.pDLMInfo, sIME.dwDLMSize, 0));

    if (AWDLMInit_eStatus) {
        LOG_DBG("Init::AWDLMInit_eStatus: [%d]", AWDLMInit_eStatus);
        return nullptr;
    }

	ET9STATUS AWDLMRegister_eStatus(ET9AWDLMRegisterForRequests(&sIME.sLingInfo, ET9Handle_DLM_Request, &sIME));

    if (AWDLMRegister_eStatus) {
        LOG_DBG("Init::AWDLMRegister_eStatus: [%d]", AWDLMRegister_eStatus);
        return nullptr;
    }

	ET9STATUS AWASDBInit_eStatus(ET9AWASDBInit(&sIME.sLingInfo, (ET9AWASDBInfo*)sIME.pbASdb, ASDB_SIZE));

    if (AWASDBInit_eStatus) {
        LOG_DBG("Init::AWASDBInit_eStatus: [%d]", AWASDBInit_eStatus);
        return nullptr;
    }

	ET9STATUS AWClearNextWord_eStatus(ET9AWClearNextWordPrediction(&sIME.sLingInfo));

    if (AWClearNextWord_eStatus) {
        LOG_DBG("Init::AWClearNextWord_eStatus: [%d]", AWClearNextWord_eStatus);
        return nullptr;
    }

	ET9STATUS AWSetWordCompl_eStatus(ET9AWSetWordCompletionPoint(&sIME.sLingInfo, 4));

    if (AWSetWordCompl_eStatus) {
        LOG_DBG("Init::AWSetWordCompl_eStatus: [%d]", AWSetWordCompl_eStatus);
        return nullptr;
    }

	ET9STATUS AWSetSpell_eStatus(ET9AWSetSpellCorrectionMode(&sIME.sLingInfo, ET9ASPCMODE_REGIONAL, 0));

    if (AWSetSpell_eStatus) {
        LOG_DBG("Init::AWSetSpell_eStatus: [%d]", AWSetSpell_eStatus);
        return nullptr;
    }

	ET9STATUS Init_eStatus(ET9KDB_Init(&sIME.sKdbInfo, &sIME.sWordSymbInfo, GENERIC_HQR, 0, 0, 0, ET9KDBLoad, &ET9Handle_KDB_Request, &sIME));

    if (Init_eStatus) {
        LOG_DBG("Init::Init_eStatus: [%d]", Init_eStatus);
        return nullptr;
    }

	ET9STATUS Validate_eStatus(ET9KDB_Validate(&sIME.sKdbInfo, GENERIC_HQR, ET9KDBLoad));

    if (Validate_eStatus) {
        LOG_DBG("Init::Validate_eStatus: [%d]", Validate_eStatus);
        return nullptr;
    }

	ET9STATUS Validate_HPD_eStatus(ET9KDB_Validate(&sIME.sKdbInfo, ENGLISH_HPD, ET9KDBLoad));

    if (Validate_HPD_eStatus) {
        LOG_DBG("Init::Validate_HPD_eStatus: [%d]", Validate_HPD_eStatus);
        return nullptr;
    }

    ET9U16  wPageNum;
    ET9U16  wSecondPageNum;
    ET9KDB_GetPageNum(&sIME.sKdbInfo, &wPageNum, &wSecondPageNum);
    sIME.eInputMode = I_HPD;
    ET9KDB_SetKdbNum(&sIME.sKdbInfo, ((sIME.sLingCmnInfo.dwFirstLdbNum & ET9PLIDMASK) | ET9SKIDPhonePad), wPageNum, ((sIME.sLingCmnInfo.dwSecondLdbNum & ET9PLIDMASK) | ET9SKIDPhonePad), wSecondPageNum);

	ET9STATUS SetRegionalMode_eStatus(ET9KDB_SetRegionalMode(&sIME.sKdbInfo));

    if (SetRegionalMode_eStatus) {
        LOG_DBG("Init::SetRegionalMode_eStatus: [%d]", SetRegionalMode_eStatus);
        return nullptr;
    }

	ET9STATUS SetAmbigMode_eStatus(ET9KDB_SetAmbigMode(&sIME.sKdbInfo, 0, 0));

    if (SetAmbigMode_eStatus) {
        LOG_DBG("Init::SetAmbigMode_eStatus: [%d]", SetAmbigMode_eStatus);
        return nullptr;
    }

    PrintScreen(&sIME);

	return nullptr;
}

void
Xt9Connect::SetLetter(const unsigned long aHexPrefix, const unsigned long aHexLetter, ErrorResult& aRv)
{

	int nInputPrefix(aHexPrefix);
	LOG_DBG("SetLetter::nInputPrefix: %x", nInputPrefix);

	int nInputChar(aHexLetter);
	LOG_DBG("SetLetter::nInputChar: %x", nInputChar);

	if (nInputChar == 0x1B) {
        return;
    }

    EditorInitWord(&sIME, Xt9Connect::sWholeWord); //send init word

    EditorInitEmptyWord(&sIME, Xt9Connect::mEmptyWord); //send empty init word

    if (nInputPrefix == 0xE0) {
        switch (nInputChar) {
            case 72: /* up arrow */
                if (sIME.bTotWords && sIME.bActiveWordIndex == 0) {
                    sIME.bActiveWordIndex = (ET9U8)(sIME.bTotWords - 1);
                }
                else {
                    --sIME.bActiveWordIndex;
                }
                break;

            case 80: /* down arrow */
                ++sIME.bActiveWordIndex;
                if (sIME.bTotWords && sIME.bActiveWordIndex == sIME.bTotWords) {
                    sIME.bActiveWordIndex = 0;
                }
                break;

            case 75: /* left arrow -  */
                if (!sIME.bTotWords) {
                    EditorMoveBackward(&sIME);
                }
                break;

            case 77: /* right arrow -  */
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

                        ET9AWReselectWord(&sIME.sLingInfo, &sIME.sKdbInfo, sString.sString, sString.wLen, 0, &sIME.bTotWords, &sIME.bActiveWordIndex, &bSelectedWasAutomatic, &bWasFoundInHistory);

                        if (!bWasFoundInHistory) {
                            ET9AWSelLstBuild(&sIME.sLingInfo, &sIME.bTotWords, &sIME.bActiveWordIndex, &sIME.wGestureValue);
                        }
                    }
                }
                break;

            default:
                break;
        }
    } else {

		switch (nInputChar)
        {
            case 0x08: /* delete */
                {
                    ET9AWWordInfo *pWord;

                    if (sIME.bTotWords && !sIME.sWordSymbInfo.wNumSymbs) {
                        sIME.bTotWords = 0;
                    }
                    else if (!sIME.bSupressSubstitutions &&
                             sIME.bTotWords &&
                             !ET9AWSelLstGetWord(&sIME.sLingInfo, &pWord, sIME.bActiveWordIndex) &&
                             pWord->wSubstitutionLen) {

                        sIME.bSupressSubstitutions = 1;
                    }
                    else if (sIME.bTotWords) {
                        sIME.bSupressSubstitutions = 0;
                        ET9ClearOneSymb(&sIME.sWordSymbInfo);

                        ET9AWSelLstBuild(&sIME.sLingInfo, &sIME.bTotWords, &sIME.bActiveWordIndex, &sIME.wGestureValue);
                    }
                    else {
                        EditorDeleteChar(&sIME);
                    }
                }
                break;
                
        	case 0x20: /* space - flush */
            case 0x0D: /* enter - flush */

                AcceptActiveWord(&sIME, (nInputChar == 0x20) ? 1 : 0);

                ET9AWSelLstBuild(&sIME.sLingInfo, &sIME.bTotWords, &sIME.bActiveWordIndex, &sIME.wGestureValue);

                ET9CursorMoved(&sIME.sWordSymbInfo, 1);

                break;

            default:

            	ET9U8 bKey(CharLookup(&sIME, nInputChar));

				if (bKey != 0xFF) {

					if (bKey == 0x80) {

						const ET9INPUTSHIFTSTATE eShiftState = ET9SHIFT_STATE(&sIME.sWordSymbInfo);

						ET9AddExplicitSymb(&sIME.sWordSymbInfo, (ET9SYMB)nInputChar, 0, eShiftState, sIME.bActiveWordIndex);
					}
					else {

						ET9SYMB sFunctionKey;

						if (ET9_KDB_MULTITAP_MODE(sIME.sKdbInfo.dwStateBits)) {

							if ((sIME.dwMultitapStartTime == 0) || (sIME.bLastKeyMT != bKey)) {
								sIME.bLastKeyMT = bKey;
							}
							else if ((GetTickCount() - sIME.dwMultitapStartTime) >= MT_TIMEOUT_TIME) {
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