#include "nsThreadUtils.h"
#include "nsXPCOMCIDInternal.h"

#include "SpeechRecognitionAdaptorService.h"

#include "SpeechRecognition.h"
#include "SpeechRecognitionAlternative.h"
#include "SpeechRecognitionResult.h"
#include "SpeechRecognitionResultList.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "nsServiceManagerUtils.h"


using namespace voiceEngineAdaptor;

namespace mozilla {

using namespace dom;
static const uint16_t skBytesPerSample = 2;
static const uint16_t skFrequency = 16000;
static const uint16_t skChannels = 1;

// using a static object first, if we have memory problem. We may need to
// initialize it on the fly.
voiceEngineApi sApi;

class DecodeResultTask : public nsRunnable
{
public:
  DecodeResultTask(const nsString& hypstring,
                   WeakPtr<dom::SpeechRecognition> recognition,
                   nsIThread* thread)
      : mResult(hypstring),
        mRecognition(recognition),
        mWorkerThread(thread)
  {
    MOZ_ASSERT(
      !NS_IsMainThread()); // This should be running on the worker thread
  }

  NS_IMETHOD
  Run()
  {
    MOZ_ASSERT(NS_IsMainThread()); // This method is supposed to run on the main
                                   // thread!

    // Declare javascript result events
    RefPtr<SpeechEvent> event = new SpeechEvent(
      mRecognition, SpeechRecognition::EVENT_RECOGNITIONSERVICE_FINAL_RESULT);
    SpeechRecognitionResultList* resultList =
      new SpeechRecognitionResultList(mRecognition);
    SpeechRecognitionResult* result = new SpeechRecognitionResult(mRecognition);
    SpeechRecognitionAlternative* alternative =
      new SpeechRecognitionAlternative(mRecognition);

    alternative->mTranscript = mResult;
    alternative->mConfidence = 100;

    result->mItems.AppendElement(alternative);
    resultList->mItems.AppendElement(result);

    event->mRecognitionResultList = resultList;
    NS_DispatchToMainThread(event);

    // If we don't destroy the thread when we're done with it, it will hang
    // around forever... bad!
    // But thread->Shutdown must be called from the main thread, not from the
    // thread itself.
    return mWorkerThread->Shutdown();
  }

private:
  nsString mResult;
  WeakPtr<dom::SpeechRecognition> mRecognition;
  nsCOMPtr<nsIThread> mWorkerThread;
};

class DecodeTask : public nsRunnable
{
public:
  DecodeTask(WeakPtr<dom::SpeechRecognition> recogntion,
             const nsTArray<int16_t>& audiovector, engineAdaptor aAdaptor)
      : mRecognition(recogntion), mAudiovector(audiovector), mAdaptor(aAdaptor)
  {
    mWorkerThread = do_GetCurrentThread();
  }

  NS_IMETHOD
  Run()
  {
    const uint32_t bufSize = 2048;
    char output[bufSize] = "";
    if (sApi.mReady) {
      recogResult ret = sApi.processAudio(mAdaptor, (char* )&mAudiovector[0],
                                     mAudiovector.Length(), output, bufSize);
      if (ret == eRecogSuccess) {
        nsCOMPtr<nsIRunnable> resultrunnable =
          new DecodeResultTask(NS_ConvertUTF8toUTF16(output), mRecognition, mWorkerThread);
        NS_DispatchToMainThread(resultrunnable);
      } else {
      }
    } else {
      MOZ_ASSERT("sApi should be ready here!!!!");
    }

    mAudiovector.Clear();
    return NS_OK;
  }

private:
  WeakPtr<dom::SpeechRecognition> mRecognition;
  nsTArray<int16_t> mAudiovector;
  engineAdaptor mAdaptor;
  nsCOMPtr<nsIThread> mWorkerThread;
};

NS_IMPL_ISUPPORTS(SpeechRecognitionAdaptorService, nsISpeechRecognitionService, nsIObserver)

SpeechRecognitionAdaptorService::SpeechRecognitionAdaptorService() : mSpeexState(nullptr)
{
  if (!sApi.mReady) {
    sApi.Init();
  }

  if (sApi.mReady) {
    sApi.enableEngine(&mAdaptor, skChannels, skFrequency, skBytesPerSample);
  }
}

SpeechRecognitionAdaptorService::~SpeechRecognitionAdaptorService()
{
  mSpeexState = nullptr;
  if (mAdaptor) {
    if (sApi.mReady) {
      sApi.disableEngine(&mAdaptor);
    } else {
      MOZ_ASSERT("sApi should be ready here !!!");
    }
  }
}


NS_IMETHODIMP
SpeechRecognitionAdaptorService::Initialize(WeakPtr<SpeechRecognition> aSpeechRecognition)
{
  if (mAdaptor == nullptr) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mRecognition = aSpeechRecognition;
  mAudioVector.Clear();
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognitionAdaptorService::ProcessAudioSegment(AudioSegment* aAudioSegment, int32_t aSampleRate)
{
  if (!mSpeexState) {
    mSpeexState = speex_resampler_init(skChannels, aSampleRate, skFrequency,
                                       SPEEX_RESAMPLER_QUALITY_MAX, nullptr);
  }
  aAudioSegment->ResampleChunks(mSpeexState, aSampleRate, skFrequency);

  AudioSegment::ChunkIterator iterator(*aAudioSegment);

  while (!iterator.IsEnded()) {
    mozilla::AudioChunk& chunk = *(iterator);
    MOZ_ASSERT(chunk.mBuffer);
    const int16_t* buf = static_cast<const int16_t*>(chunk.mChannelData[0]);

    for (int i = 0; i < iterator->mDuration; i++) {
      mAudioVector.AppendElement((int16_t)buf[i]);
    }
    iterator.Next();
  }
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognitionAdaptorService::SoundEnd()
{
  speex_resampler_destroy(mSpeexState);
  mSpeexState = nullptr;

  // To create a new thread, get the thread manager
  nsCOMPtr<nsIThreadManager> tm = do_GetService(NS_THREADMANAGER_CONTRACTID);
  nsCOMPtr<nsIThread> decodethread;
  nsresult rv = tm->NewThread(0, 0, getter_AddRefs(decodethread));
  if (NS_FAILED(rv)) {
    // In case of failure, call back immediately with an empty string which
    // indicates failure
    return NS_OK;
  }

  nsCOMPtr<nsIRunnable> r =
    new DecodeTask(mRecognition, mAudioVector, mAdaptor);
  decodethread->Dispatch(r, nsIEventTarget::DISPATCH_NORMAL);
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognitionAdaptorService::ValidateAndSetGrammarList(mozilla::dom::SpeechGrammar*, nsISpeechGrammarCompilationCallback*)
{
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognitionAdaptorService::Abort()
{
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognitionAdaptorService::Observe(nsISupports* aSubject, const char* aTopic, const char16_t* aData)
{
  return NS_OK;
}
} // namespace mozilla
