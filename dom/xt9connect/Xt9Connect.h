#ifndef mozilla_dom_xt9connect_Xt9Connect_h
#define mozilla_dom_xt9connect_Xt9Connect_h

#include "nsCOMPtr.h"
#include "nsPIDOMWindow.h"
#include "nsISupports.h"
#include "nsWrapperCache.h"
#include "nsCycleCollectionParticipant.h"
#include "nsAutoPtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "MainThreadUtils.h"

extern "C" {
#include "et9api.h"
}

#include "l0109b00.h"

#include "l0609b00.h"
#include "l09255b00.h"

#include <sys/time.h>
#include <stdio.h>

#include <android/log.h>
#include <string.h>
#include <errno.h>

#define LOG_DBG(args...)  __android_log_print(ANDROID_LOG_DEBUG, "Debug::Xt9Connect::" , ## args)

namespace mozilla {
namespace dom {

static const ET9SYMB psEmptyString[] = { 0 };
static const unsigned char pcMapKeyHQR[] =
{ 12, 25, 23, 14, 4, 15, 16, 17, 9, 18, 19, 20, 27, 26, 10, 11, 2, 5, 13, 6, 8, 24, 3, 22, 7, 21 };
static const unsigned char pcMapKeyHPD[] =
{ 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 };

static const ET9U32 GENERIC_HQR        = ET9PLIDNull | ET9SKIDATQwertyReg;
static const ET9U32 ENGLISH_HPD        = ET9PLIDEnglish | ET9SKIDPhonePad;
static const ET9U16 SEL_LIST_SIZE      = 32;
static const ET9U32 ASDB_SIZE          = (10 * 1024);
static const ET9U32 BUFFER_LEN_MAX     = 2000;
static const ET9U16 ET9XmlLayoutWidth  = 0;
static const ET9U16 ET9XmlLayoutHeight = 0;
static const ET9U16 MT_TIMEOUT_TIME    = 400;

typedef uint32_t DWORD;

typedef enum { I_HQR, I_HQD, I_HPD, I_EXP } INPUTMODE;

typedef struct {

	ET9SYMB             psBuffer[BUFFER_LEN_MAX];
	ET9INT              snBufferLen;
	ET9INT              snCursorPos;

} demoEditorInfo;

typedef struct {
	ET9AWLingInfo       sLingInfo;
	ET9AWLingCmnInfo    sLingCmnInfo;

	ET9WordSymbInfo     sWordSymbInfo;

	ET9KDBInfo          sKdbInfo;

	ET9U32              dwDLMSize;
	_ET9DLM_info        *pDLMInfo;

	FILE                *pfEventFile;

	ET9U8               pbASdb[ASDB_SIZE];

	ET9U8               bTotWords;
	ET9U8               bActiveWordIndex;

	ET9U16              wGestureValue;

	INPUTMODE           eInputMode;
	ET9BOOL             bSupressSubstitutions;
	ET9U8               bLastKeyMT;
	DWORD               dwMultitapStartTime;

	demoEditorInfo      *pEditor;

	ET9SimpleWord       sExplicitLearningWord;
} demoIMEInfo;

uint32_t GetTickCount();

void EditorInsertWord(demoIMEInfo * const pIME, ET9AWWordInfo *pWord, ET9BOOL bSupressSubstitutions);

void EditorDeleteChar(demoIMEInfo * const pIME);

void EditorMoveBackward(demoIMEInfo * const pIME);

void EditorMoveForward(demoIMEInfo * const pIME);

void AcceptActiveWord(demoIMEInfo * const pIME, const ET9BOOL bAddSpace);

ET9STATUS ET9Handle_IMU_Request(ET9WordSymbInfo * const pWordSymbInfo,
                                ET9_Request     * const pRequest);

ET9STATUS ET9FARCALL ET9KDBLoad(ET9KDBInfo    * const pKdbInfo,
                        	    const ET9U32          dwKdbNum,
                        	    const ET9U16          wPageNum);

ET9STATUS ET9FARCALL ET9AWLdbReadData(ET9AWLingInfo *pLingInfo, 
                                      ET9U8         * ET9FARDATA *ppbSrc, 
                                      ET9U32        *pdwSizeInBytes);

ET9STATUS ET9Handle_KDB_Request(ET9KDBInfo      * const pKdbInfo,
                                ET9WordSymbInfo * const pWordSymbInfo,
                                ET9KDB_Request  * const pRequest);

ET9STATUS ET9Handle_DLM_Request(void                    * const pHandlerInfo,
                                ET9AWDLM_RequestInfo    * const pRequest);

ET9U8 CharLookup(demoIMEInfo     * const pIME,
                 const int               nChar);

void PrintLanguage(const ET9U32 dwLanguage);

void PrintState(demoIMEInfo *pIME);

void PrintExplicitLearning(demoIMEInfo *pIME);

void PrintCandidateList(demoIMEInfo *pIME);

void PrintEditorBuffer(demoEditorInfo *pEditor);

void PrintScreen(demoIMEInfo *pIME);

class Xt9Connect MOZ_FINAL : public nsISupports, public nsWrapperCache
{
	public:
		NS_DECL_CYCLE_COLLECTING_ISUPPORTS
		NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(Xt9Connect)

		static already_AddRefed<Xt9Connect> Constructor(const GlobalObject& aGlobal,
														ErrorResult& aRv);

		void GetWholeWord(nsAString& aResult)
		{
			CopyASCIItoUTF16(mWholeWord, aResult);
			mWholeWord.Assign("");
		}

		void GetCandidateWord(nsAString& aResult)
		{
			CopyASCIItoUTF16(mCandidateWord, aResult);
			mCandidateWord.Assign("");
		}

		uint16_t TotalWord()
		{
			return mTotalWord;
		}

		virtual JSObject* WrapObject(JSContext* aCx) MOZ_OVERRIDE;

		nsCOMPtr<nsPIDOMWindow> mWindow;
		nsPIDOMWindow* GetOwner() const { return mWindow; }

		nsPIDOMWindow* GetParentObject()
		{
			return GetOwner();
		}

		explicit Xt9Connect(nsPIDOMWindow* aWindow);

		static already_AddRefed<Xt9Connect> Init(ErrorResult& aRv);

		static void SetLetter(const unsigned long aHexPrefix, const unsigned long aHexLetter, ErrorResult& aRv);

		static nsCString mWholeWord;
		static nsCString mCandidateWord;
		static uint16_t mTotalWord;

	private:

		unsigned long mHexLetter;

		~Xt9Connect();
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_xt9connect_Xt9Connect_h