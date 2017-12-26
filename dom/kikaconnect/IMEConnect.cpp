/* (c) 2017 KAI OS TECHNOLOGIES (HONG KONG) LIMITED All rights reserved. This
 * file or any portion thereof may not be reproduced or used in any manner
 * whatsoever without the express written permission of KAI OS TECHNOLOGIES
 * (HONG KONG) LIMITED. KaiOS is the trademark of KAI OS TECHNOLOGIES (HONG KONG)
 * LIMITED or its affiliate company and may be registered in some jurisdictions.
 * All other trademarks are the property of their respective owners.
 */

#include "mozilla/dom/IMEConnect.h"
#include "mozilla/dom/IMEConnectBinding.h"

namespace mozilla {
namespace dom {


nsString IMEConnect::mCandidateWord;
uint16_t IMEConnect::mTotalWord;
uint16_t IMEConnect::mTotalGroup;
uint16_t IMEConnect::mActiveWordIndex;
uint16_t IMEConnect::mActiveGroupIndex;
uint8_t  IMEConnect::mActiveSelectionBar;
uint32_t IMEConnect::mCurrentLID;
vector<wchar_t> IMEConnect::mKeyBuff;
CandidateCH IMEConnect::mCandVoca;
CandidateCH IMEConnect::mCandGroup;


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
IMEConnect::Constructor(const GlobalObject& aGlobal, ErrorResult& aRv)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<IMEConnect> xt_object = new IMEConnect(window);
  aRv = xt_object->Init((eKeyboardT9 << KEYBOARD_ID_SHIFT) | eImeEnglishUs);

  return xt_object.forget();
}

already_AddRefed<IMEConnect>
IMEConnect::Constructor(const GlobalObject& aGlobal, uint32_t aLid, ErrorResult& aRv)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<IMEConnect> xt_object = new IMEConnect(window);
  aRv = xt_object->Init(aLid);

  return xt_object.forget();
}

nsresult
IMEConnect::Init(uint32_t aLid)
{
  SetLanguage(aLid);
  return NS_OK;
}

wchar_t WcharToLower(wchar_t wch)
{
  if (wch <= 'Z' && wch >= 'A') {
    return wch - ('Z'-'z');
  }
  return wch;
}

wchar_t* WstrToLower(wchar_t *str)
{
  wchar_t *p = (wchar_t*)str;

  while (*p) {
    *p = WcharToLower(*p);
    p++;
  }

  return str;
}

void PackCandidates()
{
  uint8_t imeId = IMEConnect::mCurrentLID & IQQI_IME_ID_MASK;
  uint8_t keyboardId = (IMEConnect::mCurrentLID & KEYBOARD_ID_MASK) >> KEYBOARD_ID_SHIFT;
  bool isGroupSupported = (imeId == eImeChineseCn || imeId == eImeChineseTw) && (keyboardId == eKeyboardT9);

  IMEConnect::mCandidateWord.AssignLiteral("");

  /* GroupBar Packing */
  if (isGroupSupported) {
    for (int i=0; i<IMEConnect::mTotalGroup; i++) {
      nsString jsWord;

      KIKA_LOGD("PackCandidates::Group candidate[%d] = %ls", i, IMEConnect::mCandGroup.record(i));

      if (i == IMEConnect::mActiveGroupIndex && IMEConnect::mActiveSelectionBar == eSelectionBarGroup) {
        jsWord.AppendLiteral(">");
      }

      for (int j=0; j<IMEConnect::mCandGroup.vlen(i); j++) {
        jsWord.Append((PRUnichar)IMEConnect::mCandGroup.record(i)[j]);
      }

      jsWord.AppendLiteral("|");

      if (i == IMEConnect::mTotalGroup - 1) {
        jsWord.AppendLiteral("|");
      }

      IMEConnect::mCandidateWord.Append(jsWord);
    }
  }

  /* WordBar Packing */
  for (int i=0; i<IMEConnect::mTotalWord; i++) {
    nsString jsWord;

    KIKA_LOGD("PackCandidates::candidate[%d] = %ls", i, IMEConnect::mCandVoca.record(i));

    if (i == IMEConnect::mActiveWordIndex && IMEConnect::mActiveSelectionBar == eSelectionBarWord) {
      jsWord.AppendLiteral(">");
    }

    for (int j=0; j<IMEConnect::mCandVoca.vlen(i); j++) {
      jsWord.Append((PRUnichar)IMEConnect::mCandVoca.record(i)[j]);
    }

    jsWord.AppendLiteral("|");

    IMEConnect::mCandidateWord.Append(jsWord);
  }
}

void FetchCandidates()
{
  uint8_t imeId = IMEConnect::mCurrentLID & IQQI_IME_ID_MASK;
  uint8_t keyboardId = (IMEConnect::mCurrentLID & KEYBOARD_ID_MASK) >> KEYBOARD_ID_SHIFT;
  bool isGroupSupported = (imeId == eImeChineseCn || imeId == eImeChineseTw) && (keyboardId == eKeyboardT9);
  wstring wsKeyin;

  IMEConnect::mKeyBuff.push_back(0x0);
  wsKeyin = WstrToLower(reinterpret_cast<wchar_t*>(&IMEConnect::mKeyBuff[0]));
  IMEConnect::mKeyBuff.pop_back();

  if (isGroupSupported) {
    IMEConnect::mCandGroup.alloc(CANDIDATE_MAX_ROW, CANDIDATE_MAX_COL);
    IMEConnect::mTotalGroup = IQQI_GetGrouping(imeId, (wchar_t *)wsKeyin.c_str(), 0, CANDIDATE_MAX_ROW, IMEConnect::mCandGroup.pointer());

    if (IMEConnect::mTotalGroup > 0) {
      wsKeyin.append(L":");
      wsKeyin.append(IMEConnect::mCandGroup.record(IMEConnect::mActiveGroupIndex));

      IMEConnect::mCandVoca.alloc(CANDIDATE_MAX_ROW_ZH, CANDIDATE_MAX_COL);
      IQQI_GetCandidates(imeId, (wchar_t *)wsKeyin.c_str(), false, 3, 0, CANDIDATE_MAX_ROW_ZH, IMEConnect::mCandVoca.pointer());
    }
  } else {
    IMEConnect::mCandVoca.alloc(CANDIDATE_MAX_ROW, CANDIDATE_MAX_COL);
    IQQI_GetCandidates(imeId, (wchar_t *)wsKeyin.c_str(), false, 3, 0, CANDIDATE_MAX_ROW, IMEConnect::mCandVoca.pointer());
  }

  IMEConnect::mTotalWord = IQQI_GetCandidateCount(0, (wchar_t *)wsKeyin.c_str(), false, 0);

  PackCandidates();
}

void
IMEConnect::SetLetterMultiTap(const unsigned long aKeyCode, const unsigned long aTapCount, unsigned short aPrevUnichar)
{
  uint8_t imeId = IMEConnect::mCurrentLID & IQQI_IME_ID_MASK;
  uint8_t keyboardId = (IMEConnect::mCurrentLID & KEYBOARD_ID_MASK) >> KEYBOARD_ID_SHIFT;

  if (keyboardId != eKeyboardT9) {
    KIKA_LOGW("SetLetterMultiTap::Not support in current keyboardId = 0x%x", keyboardId);
    return;
  }

  if (aKeyCode < 0x30 || aKeyCode > 0x39) { // 0~9
    KIKA_LOGW("SetLetterMultiTap::Invalid aKeyCode = 0x%lx", aKeyCode);
    return;
  }

  KIKA_LOGD("SetLetterMultiTap::aKeyCode = 0x%x, count = %d", (int)aKeyCode, (int)aTapCount);

  wchar_t *keyin = IQQI_MultiTap_Input(imeId, (int)aKeyCode, (int)aTapCount);

  if (keyin == 0) {
    KIKA_LOGW("SetLetterMultiTap::MultiTap invalid keyin");
    return;
  }

  IMEConnect::mCandidateWord.AssignLiteral("");

  KIKA_LOGD("SetLetterMultiTap::keyin = %ls", keyin);

  nsString jsWord;
  for (int i=0; keyin[i]!=0; i++) {
    mKeyBuff.push_back(keyin[i]);
    jsWord.Append((PRUnichar)keyin[i]);
  }
  IMEConnect::mCandidateWord.Append(jsWord);
}

void
IMEConnect::SetLetter(const unsigned long aHexPrefix, const unsigned long aHexLetter, ErrorResult& aRv)
{
  uint8_t imeId = IMEConnect::mCurrentLID & IQQI_IME_ID_MASK;
  uint8_t keyboardId = (IMEConnect::mCurrentLID & KEYBOARD_ID_MASK) >> KEYBOARD_ID_SHIFT;
  bool isGroupSupported = (imeId == eImeChineseCn || imeId == eImeChineseTw) && (keyboardId == eKeyboardT9);
  bool shouldRefetch = false;

  KIKA_LOGD("SetLetter::aHexPrefix = 0x%x, aHexLetter = 0x%x", (int)aHexPrefix, (int)aHexLetter);

  switch (aHexLetter) {
    case 0x26: // up arrow
    case 0x28: // down arrow
      if (isGroupSupported) {
        mActiveSelectionBar = (mActiveSelectionBar == eSelectionBarWord) ? eSelectionBarGroup : eSelectionBarWord;
        PackCandidates();
      }
      break;
    case 0x25: // left arrow
      if (isGroupSupported && (mActiveSelectionBar == eSelectionBarGroup)) {
        mActiveGroupIndex = (mActiveGroupIndex > 0 ? mActiveGroupIndex - 1 : mTotalGroup - 1);
        mActiveWordIndex = 0;
        shouldRefetch = true;
      } else {
        mActiveWordIndex = (mActiveWordIndex > 0 ? mActiveWordIndex - 1 : mTotalWord - 1);
        PackCandidates();
      }
      break;
    case 0x27: // right arrow
      if (isGroupSupported && (mActiveSelectionBar == eSelectionBarGroup)) {
        mActiveGroupIndex = (mActiveGroupIndex < mTotalGroup - 1 ? mActiveGroupIndex + 1 : 0);
        mActiveWordIndex = 0;
        shouldRefetch = true;
      } else {
        mActiveWordIndex = (mActiveWordIndex < mTotalWord - 1 ? mActiveWordIndex + 1 : 0);
        PackCandidates();
      }
      break;
    case 0x0D: // enter
    case 0x20: // space
      mKeyBuff.clear();
      IMEConnect::mCandidateWord.AssignLiteral("");
      break;
    case 0x08: // backspace
      if (mKeyBuff.size() > 0) {
        mKeyBuff.pop_back();
        mActiveWordIndex = 0;
        if (mKeyBuff.size() != 0) {
          shouldRefetch = true;
          if (isGroupSupported) {
            mActiveGroupIndex = 0;
            mActiveSelectionBar = eSelectionBarGroup;
          }
        } else {
          mKeyBuff.clear();
          IMEConnect::mCandidateWord.AssignLiteral("");
        }
      }
      break;
    default:
      if (keyboardId == eKeyboardT9) {
        if (aHexLetter >= 0x30 && aHexLetter <= 0x39) { // 0~9
          KIKA_LOGD("SetLetter::push aHexLetter = 0x%x", (int)aHexLetter);
          mKeyBuff.push_back(aHexLetter);
          mActiveWordIndex = 0;
          shouldRefetch = true;
          if (isGroupSupported) {
            mActiveGroupIndex = 0;
            mActiveSelectionBar = eSelectionBarGroup;
          }
        }
      } else if (keyboardId == eKeyboardQwerty) {
        if (aHexLetter >= 0x41 && aHexLetter <= 0x5A) { // A~Z
          KIKA_LOGD("SetLetter::push aHexLetter = 0x%x", (int)aHexLetter);
          mKeyBuff.push_back(aHexLetter);
          mActiveWordIndex = 0;
          shouldRefetch = true;
          if (isGroupSupported) {
            mActiveGroupIndex = 0;
            mActiveSelectionBar = eSelectionBarGroup;
          }
        }
      }
      break;
  }

  if (shouldRefetch) {
    FetchCandidates();
  }
}

uint32_t
IMEConnect::SetLanguage(const uint32_t aLid)
{
  int ret;
  char IME_ErrorList[32] = {0};
  uint8_t imeId = aLid & IQQI_IME_ID_MASK;
  uint8_t keyboardId = (aLid & KEYBOARD_ID_MASK) >> KEYBOARD_ID_SHIFT;
  string dictPath = DICT_ROOT_PATH;

  if (imeId <= eImeStart || imeId >= eImeEnd) {
    KIKA_LOGW("SetLanguage::imeId = 0x%x not found", imeId);
    return mCurrentLID;
  }

  if (keyboardId <= eKeyboardStart || keyboardId >= eKeyboardEnd) {
    KIKA_LOGW("SetLanguage::keyboardId = 0x%x not found", keyboardId);
    return mCurrentLID;
  }

  KIKA_LOGD("SetLanguage::keyboardId = 0x%x, imeId = 0x%x", keyboardId, imeId);

  IQQI_SetOption(eOptionKeyboardMode, keyboardId);

  dictPath += DICT_TABLE[imeId];
  ret = IQQI_Initial(imeId, (char*)dictPath.c_str(), NULL, NULL, IME_ErrorList);
  if (ret != KIKA_OK) {
    KIKA_LOGW("SetLanguage::IQQI_initial() dictPath = %s, ret = %d", dictPath.c_str(), ret);
  }

  mCandidateWord.AssignLiteral("");
  mKeyBuff.clear();
  mActiveWordIndex = 0;
  mActiveGroupIndex = 0;
  mActiveSelectionBar = eSelectionBarWord;
  mCandVoca.empty();
  mCandGroup.empty();
  mCurrentLID = aLid;

  KIKA_LOGD("SetLanguage::mCurrentLID = 0x%x" , mCurrentLID);

  return mCurrentLID;
}

JSObject*
IMEConnect::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return mozilla::dom::IMEConnectBinding::Wrap(aCx, this, aGivenProto);
}

IMEConnect::~IMEConnect()
{
  IQQI_Free();
}


} // namespace dom
} // namespace mozilla
