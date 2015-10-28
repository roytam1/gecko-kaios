/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nsWindow.h"

#include "mozilla/DebugOnly.h"

#include <fcntl.h>
#include <map>

#include "android/log.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/dom/TabParent.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Services.h"
#include "mozilla/FileUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "GLContextProvider.h"
#include "GLContext.h"
#include "GLContextEGL.h"
#include "nsLayoutUtils.h"
#include "nsAppShell.h"
#include "nsDOMTokenList.h"
#include "nsIFrame.h"
#include "nsScreenManagerGonk.h"
#include "nsTArray.h"
#include "nsIWidgetListener.h"
#include "ClientLayerManager.h"
#include "BasicLayers.h"
#include "libdisplay/GonkDisplay.h"
#include "mozilla/TextEvents.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/APZCTreeManager.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/CompositorOGL.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/LayerManagerComposite.h"
#include "mozilla/layers/LayerTransactionChild.h"
#include "mozilla/TouchEvents.h"
#include "HwcComposer2D.h"
#include "nsImageLoadingContent.h"

#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk" , ## args)
#define LOGW(args...) __android_log_print(ANDROID_LOG_WARN, "Gonk", ## args)
#define LOGE(args...) __android_log_print(ANDROID_LOG_ERROR, "Gonk", ## args)

#define IS_TOPLEVEL() (mWindowType == eWindowType_toplevel || mWindowType == eWindowType_dialog)

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::hal;
using namespace mozilla::gfx;
using namespace mozilla::gl;
using namespace mozilla::layers;
using namespace mozilla::widget;

static nsWindow *gFocusedWindow = nullptr;

NS_IMPL_ISUPPORTS_INHERITED0(nsWindow, nsBaseWidget)

nsWindow::nsWindow()
{
    RefPtr<nsScreenManagerGonk> screenManager = nsScreenManagerGonk::GetInstance();
    screenManager->Initialize();

    // This is a hack to force initialization of the compositor
    // resources, if we're going to use omtc.
    //
    // NB: GetPlatform() will create the gfxPlatform, which wants
    // to know the color depth, which asks our native window.
    // This has to happen after other init has finished.
    gfxPlatform::GetPlatform();
    if (!ShouldUseOffMainThreadCompositing()) {
        MOZ_CRASH("How can we render apps, then?");
    }
}

nsWindow::~nsWindow()
{
    ErrorResult rv;
    if (mWidgetListener) {
      nsIPresShell* presShell = mWidgetListener->GetPresShell();
      if (presShell && presShell->GetDocument()) {
         presShell->GetDocument()->RemoveAnonymousContent(*mCursorElementHolder, rv);
      }
    }

    if (mCursorElementHolder.get()) {
      mCursorElementHolder = nullptr;
    }

    if (mScreen->IsPrimaryScreen()) {
        mComposer2D->SetCompositorBridgeParent(nullptr);
    }
}

void
nsWindow::DoDraw(void)
{
    if (!hal::GetScreenEnabled()) {
        gDrawRequest = true;
        return;
    }

    uint32_t screenNums = 0;
    RefPtr<nsScreenManagerGonk> screenManager = nsScreenManagerGonk::GetInstance();
    screenManager->GetNumberOfScreens(&screenNums);

    while (screenNums--) {
        nsCOMPtr<nsIScreen> screen;
        screenManager->ScreenForId(screenNums, getter_AddRefs(screen));
        MOZ_ASSERT(screen);
        if (!screen) {
            continue;
        }

        const nsTArray<nsWindow*>& windows =
          static_cast<nsScreenGonk*>(screen.get())->GetTopWindows();
        if (windows.IsEmpty()) {
            continue;
        }

        /* Add external screen when the external fb is available. The AddScreen
           should be called after shell.js is loaded to receive the
           display-changed event. */
        if (!screenManager->IsScreenConnected(GonkDisplay::DISPLAY_EXTERNAL) &&
            screenNums == 0 && GetGonkDisplay()->IsExtFBDeviceEnabled()) {
            screenManager->AddScreen(GonkDisplay::DISPLAY_EXTERNAL);
        }

        nsWindow *targetWindow = (nsWindow *)windows[0];
        while (targetWindow->GetLastChild()) {
            targetWindow = (nsWindow *)targetWindow->GetLastChild();
        }

        nsIWidgetListener* listener = targetWindow->GetWidgetListener();
        if (listener) {
            listener->WillPaintWindow(targetWindow);
        }

        LayerManager* lm = targetWindow->GetLayerManager();
        if (mozilla::layers::LayersBackend::LAYERS_CLIENT == lm->GetBackendType()) {
            // No need to do anything, the compositor will handle drawing
        } else {
           NS_RUNTIMEABORT("Unexpected layer manager type");
        }

        listener = targetWindow->GetWidgetListener();
        if (listener) {
            listener->DidPaintWindow();
        }
    }
}

void
nsWindow::ConfigureAPZControllerThread()
{
    APZThreadUtils::SetControllerThread(CompositorBridgeParent::CompositorLoop());
}

void
nsWindow::SetMouseCursorPosition(const ScreenIntPoint& aScreenIntPoint)
{
    // The only implementation of nsIWidget::SetMouseCursorPosition in nsWindow
    // is for remote control.
    if (gFocusedWindow) {
        // NB: this is a racy use of gFocusedWindow.  We assume that
        // our one and only top widget is already in a stable state by
        // the time we start receiving mousemove events.
        gFocusedWindow->SetScreenIntPoint(aScreenIntPoint);
        gFocusedWindow->mCompositorBridgeParent->InvalidateOnCompositorThread();
        gFocusedWindow->mCompositorBridgeParent->ScheduleRenderOnCompositorThread();
    }
}

/*static*/ nsEventStatus
nsWindow::DispatchKeyInput(WidgetKeyboardEvent& aEvent)
{
    if (!gFocusedWindow) {
        return nsEventStatus_eIgnore;
    }

    gFocusedWindow->UserActivity();

    nsEventStatus status;
    aEvent.mWidget = gFocusedWindow;
    gFocusedWindow->DispatchEvent(&aEvent, status);
    return status;
}

/*static*/ void
nsWindow::DispatchTouchInput(MultiTouchInput& aInput)
{
    APZThreadUtils::AssertOnControllerThread();

    if (!gFocusedWindow) {
        return;
    }

    gFocusedWindow->DispatchTouchInputViaAPZ(aInput);
}

/*static*/ void
nsWindow::SetMouseDevice(bool aMouse)
{
    if (gFocusedWindow) {
        gFocusedWindow->SetDrawMouse(aMouse);
        ScreenIntPoint point(0,0);
        nsWindow::NotifyHoverMove(point);
    }
}

/*static*/ void
nsWindow::NotifyHoverMove(const ScreenIntPoint& point)
{
    if (gFocusedWindow) {
        // NB: this is a racy use of gFocusedWindow.  We assume that
        // our one and only top widget is already in a stable state by
        // the time we start receiving hover-move events.
        gFocusedWindow->SetScreenIntPoint(point);
        gFocusedWindow->mCompositorBridgeParent->InvalidateOnCompositorThread();
        gFocusedWindow->mCompositorBridgeParent->ScheduleRenderOnCompositorThread();
    }
}

class DispatchTouchInputOnMainThread : public nsRunnable
{
public:
    DispatchTouchInputOnMainThread(const MultiTouchInput& aInput,
                                   const ScrollableLayerGuid& aGuid,
                                   const uint64_t& aInputBlockId,
                                   nsEventStatus aApzResponse)
      : mInput(aInput)
      , mGuid(aGuid)
      , mInputBlockId(aInputBlockId)
      , mApzResponse(aApzResponse)
    {}

    NS_IMETHOD Run() {
        if (gFocusedWindow) {
            gFocusedWindow->DispatchTouchEventForAPZ(mInput, mGuid, mInputBlockId, mApzResponse);
        }
        return NS_OK;
    }

private:
    MultiTouchInput mInput;
    ScrollableLayerGuid mGuid;
    uint64_t mInputBlockId;
    nsEventStatus mApzResponse;
};

void
nsWindow::DispatchTouchInputViaAPZ(MultiTouchInput& aInput)
{
    APZThreadUtils::AssertOnControllerThread();

    if (!mAPZC) {
        // In general mAPZC should not be null, but during initial setup
        // it might be, so we handle that case by ignoring touch input there.
        return;
    }

    // First send it through the APZ code
    mozilla::layers::ScrollableLayerGuid guid;
    uint64_t inputBlockId;
    nsEventStatus result = mAPZC->ReceiveInputEvent(aInput, &guid, &inputBlockId);
    // If the APZ says to drop it, then we drop it
    if (result == nsEventStatus_eConsumeNoDefault) {
        return;
    }

    // Can't use NS_NewRunnableMethod because it only takes up to one arg and
    // we need more. Also we can't pass in |this| to the task because nsWindow
    // refcounting is not threadsafe. Instead we just use the gFocusedWindow
    // static ptr inside the task.
    NS_DispatchToMainThread(new DispatchTouchInputOnMainThread(
        aInput, guid, inputBlockId, result));
}

void
nsWindow::DispatchTouchEventForAPZ(const MultiTouchInput& aInput,
                                   const ScrollableLayerGuid& aGuid,
                                   const uint64_t aInputBlockId,
                                   nsEventStatus aApzResponse)
{
    MOZ_ASSERT(NS_IsMainThread());
    UserActivity();

    // Convert it to an event we can send to Gecko
    WidgetTouchEvent event = aInput.ToWidgetTouchEvent(this);

    // Dispatch the event into the gecko root process for "normal" flow.
    // The event might get sent to a child process,
    // but if it doesn't we need to notify the APZ of various things.
    // All of that happens in ProcessUntransformedAPZEvent
    ProcessUntransformedAPZEvent(&event, aGuid, aInputBlockId, aApzResponse);
}

class DispatchTouchInputOnControllerThread : public Task
{
public:
    DispatchTouchInputOnControllerThread(const MultiTouchInput& aInput)
      : Task()
      , mInput(aInput)
    {}

    virtual void Run() override {
        if (gFocusedWindow) {
            gFocusedWindow->DispatchTouchInputViaAPZ(mInput);
        }
    }

private:
    MultiTouchInput mInput;
};

nsresult
nsWindow::SynthesizeNativeTouchPoint(uint32_t aPointerId,
                                     TouchPointerState aPointerState,
                                     LayoutDeviceIntPoint aPoint,
                                     double aPointerPressure,
                                     uint32_t aPointerOrientation,
                                     nsIObserver* aObserver)
{
    AutoObserverNotifier notifier(aObserver, "touchpoint");

    if (aPointerState == TOUCH_HOVER) {
        return NS_ERROR_UNEXPECTED;
    }

    if (!mSynthesizedTouchInput) {
        mSynthesizedTouchInput = MakeUnique<MultiTouchInput>();
    }

    ScreenIntPoint pointerScreenPoint = ViewAs<ScreenPixel>(aPoint,
        PixelCastJustification::LayoutDeviceIsScreenForBounds);

    // We can't dispatch mSynthesizedTouchInput directly because (a) dispatching
    // it might inadvertently modify it and (b) in the case of touchend or
    // touchcancel events mSynthesizedTouchInput will hold the touches that are
    // still down whereas the input dispatched needs to hold the removed
    // touch(es). We use |inputToDispatch| for this purpose.
    MultiTouchInput inputToDispatch;
    inputToDispatch.mInputType = MULTITOUCH_INPUT;

    int32_t index = mSynthesizedTouchInput->IndexOfTouch((int32_t)aPointerId);
    if (aPointerState == TOUCH_CONTACT) {
        if (index >= 0) {
            // found an existing touch point, update it
            SingleTouchData& point = mSynthesizedTouchInput->mTouches[index];
            point.mScreenPoint = pointerScreenPoint;
            point.mRotationAngle = (float)aPointerOrientation;
            point.mForce = (float)aPointerPressure;
            inputToDispatch.mType = MultiTouchInput::MULTITOUCH_MOVE;
        } else {
            // new touch point, add it
            mSynthesizedTouchInput->mTouches.AppendElement(SingleTouchData(
                (int32_t)aPointerId,
                pointerScreenPoint,
                ScreenSize(0, 0),
                (float)aPointerOrientation,
                (float)aPointerPressure));
            inputToDispatch.mType = MultiTouchInput::MULTITOUCH_START;
        }
        inputToDispatch.mTouches = mSynthesizedTouchInput->mTouches;
    } else {
        MOZ_ASSERT(aPointerState == TOUCH_REMOVE || aPointerState == TOUCH_CANCEL);
        // a touch point is being lifted, so remove it from the stored list
        if (index >= 0) {
            mSynthesizedTouchInput->mTouches.RemoveElementAt(index);
        }
        inputToDispatch.mType = (aPointerState == TOUCH_REMOVE
            ? MultiTouchInput::MULTITOUCH_END
            : MultiTouchInput::MULTITOUCH_CANCEL);
        inputToDispatch.mTouches.AppendElement(SingleTouchData(
            (int32_t)aPointerId,
            pointerScreenPoint,
            ScreenSize(0, 0),
            (float)aPointerOrientation,
            (float)aPointerPressure));
    }

    // Can't use NewRunnableMethod here because that will pass a const-ref
    // argument to DispatchTouchInputViaAPZ whereas that function takes a
    // non-const ref. At this callsite we don't care about the mutations that
    // the function performs so this is fine. Also we can't pass |this| to the
    // task because nsWindow refcounting is not threadsafe. Instead we just use
    // the gFocusedWindow static ptr instead the task.
    APZThreadUtils::RunOnControllerThread(new DispatchTouchInputOnControllerThread(inputToDispatch));

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Create(nsIWidget* aParent,
                 void* aNativeParent,
                 const LayoutDeviceIntRect& aRect,
                 nsWidgetInitData* aInitData)
{
    BaseCreate(aParent, aInitData);

    nsCOMPtr<nsIScreen> screen;

    uint32_t screenId = aParent ? ((nsWindow*)aParent)->mScreen->GetId() :
                                  aInitData->mScreenId;

    RefPtr<nsScreenManagerGonk> screenManager = nsScreenManagerGonk::GetInstance();
    screenManager->ScreenForId(screenId, getter_AddRefs(screen));

    mScreen = static_cast<nsScreenGonk*>(screen.get());

    mBounds = aRect;

    mParent = (nsWindow *)aParent;
    mVisible = false;

    if (!aParent) {
        mBounds = mScreen->GetRect();
    }

    mComposer2D = HwcComposer2D::GetInstance();

    if (!IS_TOPLEVEL()) {
        return NS_OK;
    }

    mScreen->RegisterWindow(this);

    Resize(0, 0, mBounds.width, mBounds.height, false);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Destroy(void)
{
    mOnDestroyCalled = true;
    mScreen->UnregisterWindow(this);
    if (this == gFocusedWindow) {
        gFocusedWindow = nullptr;
    }
    nsBaseWidget::OnDestroy();
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Show(bool aState)
{
    if (mWindowType == eWindowType_invisible) {
        return NS_OK;
    }

    if (mVisible == aState) {
        return NS_OK;
    }

    mVisible = aState;
    if (!IS_TOPLEVEL()) {
        return mParent ? mParent->Show(aState) : NS_OK;
    }

    if (aState) {
        BringToTop();
    } else {
        const nsTArray<nsWindow*>& windows =
            mScreen->GetTopWindows();
        for (unsigned int i = 0; i < windows.Length(); i++) {
            nsWindow *win = windows[i];
            if (!win->mVisible) {
                continue;
            }
            win->BringToTop();
            break;
        }
    }

    return NS_OK;
}

bool
nsWindow::IsVisible() const
{
    return mVisible;
}

NS_IMETHODIMP
nsWindow::ConstrainPosition(bool aAllowSlop,
                            int32_t *aX,
                            int32_t *aY)
{
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Move(double aX,
               double aY)
{
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Resize(double aWidth,
                 double aHeight,
                 bool   aRepaint)
{
    return Resize(0, 0, aWidth, aHeight, aRepaint);
}

NS_IMETHODIMP
nsWindow::Resize(double aX,
                 double aY,
                 double aWidth,
                 double aHeight,
                 bool   aRepaint)
{
    mBounds = LayoutDeviceIntRect(NSToIntRound(aX), NSToIntRound(aY),
                                  NSToIntRound(aWidth), NSToIntRound(aHeight));
    if (mWidgetListener) {
        mWidgetListener->WindowResized(this, mBounds.width, mBounds.height);
    }

    if (aRepaint) {
        Invalidate(mBounds);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Enable(bool aState)
{
    return NS_OK;
}

bool
nsWindow::IsEnabled() const
{
    return true;
}

NS_IMETHODIMP
nsWindow::SetFocus(bool aRaise)
{
    if (aRaise) {
        BringToTop();
    }

    if (!IS_TOPLEVEL() && mScreen->IsPrimaryScreen()) {
        // We should only set focused window on non-toplevel primary window.
        gFocusedWindow = this;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ConfigureChildren(const nsTArray<nsIWidget::Configuration>&)
{
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Invalidate(const LayoutDeviceIntRect& aRect)
{
    nsWindow *top = mParent;
    while (top && top->mParent) {
        top = top->mParent;
    }
    const nsTArray<nsWindow*>& windows = mScreen->GetTopWindows();
    if (top != windows[0] && this != windows[0]) {
        return NS_OK;
    }

    gDrawRequest = true;
    mozilla::NotifyEvent();
    return NS_OK;
}

LayoutDeviceIntPoint
nsWindow::WidgetToScreenOffset()
{
    LayoutDeviceIntPoint p(0, 0);
    nsWindow *w = this;

    while (w && w->mParent) {
        p.x += w->mBounds.x;
        p.y += w->mBounds.y;

        w = w->mParent;
    }

    return p;
}

void*
nsWindow::GetNativeData(uint32_t aDataType)
{
    switch (aDataType) {
    case NS_NATIVE_WINDOW:
        // Called before primary display's EGLSurface creation.
        return mScreen->GetNativeWindow();
    case NS_NATIVE_OPENGL_CONTEXT:
        return mScreen->GetGLContext().take();
    case NS_RAW_NATIVE_IME_CONTEXT: {
        void* pseudoIMEContext = GetPseudoIMEContext();
        if (pseudoIMEContext) {
            return pseudoIMEContext;
        }
        // There is only one IME context on Gonk.
        return NS_ONLY_ONE_NATIVE_IME_CONTEXT;
    }
    }

    return nullptr;
}

void
nsWindow::SetNativeData(uint32_t aDataType, uintptr_t aVal)
{
    switch (aDataType) {
    case NS_NATIVE_OPENGL_CONTEXT:
        GLContext* context = reinterpret_cast<GLContext*>(aVal);
        if (!context) {
            mScreen->SetEGLInfo(EGL_NO_DISPLAY,
                                EGL_NO_SURFACE,
                                nullptr);
            return;
        }
        mScreen->SetEGLInfo(GLContextEGL::Cast(context)->GetEGLDisplay(),
                            GLContextEGL::Cast(context)->GetEGLSurface(),
                            context);
        return;
    }
}

typedef mozilla::gfx::SourceSurface SourceSurface;
std::map<nsCursor, RefPtr<SourceSurface>> sCursorSourceMap;

nsString GetCursorElementClassID(nsCursor aCursor)
{
  nsString strClassID;
  switch (aCursor) {
  case eCursor_standard:
    strClassID = NS_LITERAL_STRING("std");
    break;
  case eCursor_wait:
    strClassID = NS_LITERAL_STRING("wait");
    break;
  case eCursor_grab:
    strClassID = NS_LITERAL_STRING("grab");
    break;
  case eCursor_grabbing:
    strClassID = NS_LITERAL_STRING("grabbing");
    break;
  case eCursor_select:
    strClassID = NS_LITERAL_STRING("select");
    break;
  case eCursor_hyperlink:
    strClassID = NS_LITERAL_STRING("link");
    break;
  case eCursor_vertical_text:
    strClassID = NS_LITERAL_STRING("vertical_text");
    break;
  case eCursor_spinning:
    strClassID = NS_LITERAL_STRING("spinning");
    break;
  default:
    strClassID = NS_LITERAL_STRING("std");
  }
  return strClassID;
}

nsCursor MapCursorState(nsCursor aCursor)
{
  nsCursor mappedCursor = aCursor;
  switch (mappedCursor) {
  case eCursor_standard:
  case eCursor_wait:
  case eCursor_grab:
  case eCursor_grabbing:
  case eCursor_select:
  case eCursor_hyperlink:
  case eCursor_vertical_text:
  case eCursor_spinning:
    break;
  default:
    mappedCursor = eCursor_standard;
  }
  return mappedCursor;
}
already_AddRefed<SourceSurface>
nsWindow::RestyleCursorElement(nsCursor aCursor)
{
  RefPtr<SourceSurface> source;
  nsIPresShell* presShell = mWidgetListener->GetPresShell();
  if (presShell && presShell->GetDocument()) {
    nsIDocument* doc = presShell->GetDocument();

    // Only run once during the initialization phase
    if (!mCursorElementHolder.get()) {
      nsCOMPtr<dom::Element> image = doc->CreateHTMLElement(nsGkAtoms::img);
      ErrorResult rv;
      image->ClassList()->Add(NS_LITERAL_STRING("moz-cursor"), rv);
      image->ClassList()->Add(GetCursorElementClassID(aCursor), rv);
      mCursorElementHolder = doc->InsertAnonymousContent(*image, rv);
      mLastMappedCursor = aCursor;
    } else if (mLastMappedCursor != aCursor) {
      ErrorResult rv;
      mCursorElementHolder->GetContentNode()->ClassList()->Remove(GetCursorElementClassID(mLastMappedCursor), rv);
      mCursorElementHolder->GetContentNode()->ClassList()->Add(GetCursorElementClassID(aCursor), rv);
      mLastMappedCursor = aCursor;

      /* This is a kind of trick to load cursor image in next setfocus call
       * which is triggered in next refresh driver tick, since the restyle
       * only happen in next tick
       */
      return source.forget();
    }

    if (mCursorElementHolder.get()) {
      ErrorResult rv;
      nsCOMPtr<dom::Element> element = mCursorElementHolder->GetContentNode();
      nsIFrame* frame = element->GetPrimaryFrame();
      frame = nsLayoutUtils::GetAfterFrame(frame);
      nsCOMPtr<dom::Element> cursorElement = frame->GetContent()->GetFirstChild()->AsElement();

      if (cursorElement.get()) {
        nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(cursorElement);

        RefPtr<DrawTarget> dummy;
        nsLayoutUtils::SurfaceFromElementResult res =
          nsLayoutUtils::SurfaceFromElement(imageLoader, nsLayoutUtils::SFE_WANT_FIRST_FRAME, dummy);
        source = res.GetSourceSurface();
      }
    }
  }
  return source.forget();
}
void nsWindow::UpdateCursorSourceMap(nsCursor aCursor)
{
  RefPtr<SourceSurface> source;
  // Find aCursor in mCursorMap and get img
  std::map<nsCursor, RefPtr<SourceSurface>>::iterator itr;

  nsCursor mappedCursor = MapCursorState(aCursor);
  itr = sCursorSourceMap.find(mappedCursor);

  // Can't find the cursor image based on current state
  if (itr == sCursorSourceMap.end()) {
    source = RestyleCursorElement(mappedCursor);
    if (source.get()) {
      sCursorSourceMap[mappedCursor] = source;
    }
  } else {
    source = itr->second;
  }

  if (source.get() && mCursorSource != source) {
    mCursorSource = source;
    mCursorSourceChanged = true;
  }
}

NS_IMETHODIMP
nsWindow::SetCursor(nsCursor aCursor)
{
  UpdateCursorSourceMap(aCursor);

  nsBaseWidget::SetCursor(aCursor);
  return NS_OK;
}

void KickOffComposition(LayerManager* aLayerManager)
{
  if (!aLayerManager) {
    return;
  }

  RefPtr<LayerTransactionChild> transaction;
  ShadowLayerForwarder* forwarder = aLayerManager->AsShadowForwarder();
  if (forwarder && forwarder->HasShadowManager()) {
    transaction = forwarder->GetShadowManager();
  }

  if (transaction && transaction->IPCOpen()) {
    //Trigger compostion to draw GL cursor
    transaction->SendForceComposite();
  }

}

NS_IMETHODIMP
nsWindow::DispatchEvent(WidgetGUIEvent* aEvent, nsEventStatus& aStatus)
{
    if (aEvent->mMessage == eMouseMove) {
      mCursorPos.x = aEvent->mRefPoint.x;
      mCursorPos.y = aEvent->mRefPoint.y;

      if (gfxPrefs::GLCursorEnabled()) {
        KickOffComposition(GetLayerManager());
      }

    } else if (aEvent->mMessage == eMouseExitFromWidget) {
      mCursorPos.x = -1;
      mCursorPos.y = -1;

      if (gfxPrefs::GLCursorEnabled()) {
        KickOffComposition(GetLayerManager());
      }

    }

    if (mWidgetListener) {
      aStatus = mWidgetListener->HandleEvent(aEvent, mUseAttachedEvents);
    }
    return NS_OK;
}

NS_IMETHODIMP_(void)
nsWindow::SetInputContext(const InputContext& aContext,
                          const InputContextAction& aAction)
{
    mInputContext = aContext;
}

NS_IMETHODIMP_(InputContext)
nsWindow::GetInputContext()
{
    return mInputContext;
}

NS_IMETHODIMP
nsWindow::ReparentNativeWidget(nsIWidget* aNewParent)
{
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::MakeFullScreen(bool aFullScreen, nsIScreen*)
{
    if (mWindowType != eWindowType_toplevel) {
        // Ignore fullscreen request for non-toplevel windows.
        NS_WARNING("MakeFullScreen() on a dialog or child widget?");
        return nsBaseWidget::MakeFullScreen(aFullScreen);
    }

    if (aFullScreen) {
        // Fullscreen is "sticky" for toplevel widgets on gonk: we
        // must paint the entire screen, and should only have one
        // toplevel widget, so it doesn't make sense to ever "exit"
        // fullscreen.  If we do, we can leave parts of the screen
        // unpainted.
        nsIntRect virtualBounds;
        mScreen->GetRect(&virtualBounds.x, &virtualBounds.y,
                         &virtualBounds.width, &virtualBounds.height);
        Resize(virtualBounds.x, virtualBounds.y,
               virtualBounds.width, virtualBounds.height,
               /*repaint*/true);
    }

    if (nsIWidgetListener* listener = GetWidgetListener()) {
      listener->FullscreenChanged(aFullScreen);
    }
    return NS_OK;
}

void
nsWindow::DrawWindowOverlay(LayerManagerComposite* aManager, LayoutDeviceIntRect aRect)
{
    if (aManager) {
      CompositorOGL *compositor = static_cast<CompositorOGL*>(aManager->GetCompositor());
      if (compositor) {
        if (mCursorSource.get() && mCursorSourceChanged) {
          compositor->UpdateGLCursorTexture(mCursorSource->GetDataSurface());
          mCursorSourceChanged = false;
        }
        compositor->DrawGLCursor(aRect, mCursorPos);
      }
    }
}

already_AddRefed<DrawTarget>
nsWindow::StartRemoteDrawing()
{
    RefPtr<DrawTarget> buffer = mScreen->StartRemoteDrawing();
    return buffer.forget();
}

void
nsWindow::EndRemoteDrawing()
{
    mScreen->EndRemoteDrawing();
}

float
nsWindow::GetDPI()
{
    return mScreen->GetDpi();
}

bool
nsWindow::IsVsyncSupported()
{
    return mScreen->IsVsyncSupported();
}

double
nsWindow::GetDefaultScaleInternal()
{
    float dpi = GetDPI();
    // The mean pixel density for mdpi devices is 160dpi, 240dpi for hdpi,
    // and 320dpi for xhdpi, respectively.
    // We'll take the mid-value between these three numbers as the boundary.
    if (dpi < 200.0) {
        return 1.0; // mdpi devices.
    }
    if (dpi < 280.0) {
        return 1.5; // hdpi devices.
    }
    // xhdpi devices and beyond.
    return floor(dpi / 150.0 + 0.5);
}

LayerManager *
nsWindow::GetLayerManager(PLayerTransactionChild* aShadowManager,
                          LayersBackend aBackendHint,
                          LayerManagerPersistence aPersistence,
                          bool* aAllowRetaining)
{
    if (aAllowRetaining) {
        *aAllowRetaining = true;
    }
    if (mLayerManager) {
        // This layer manager might be used for painting outside of DoDraw(), so we need
        // to set the correct rotation on it.
        if (mLayerManager->GetBackendType() == LayersBackend::LAYERS_CLIENT) {
            ClientLayerManager* manager =
                static_cast<ClientLayerManager*>(mLayerManager.get());
            uint32_t rotation = mScreen->EffectiveScreenRotation();
            manager->SetDefaultTargetConfiguration(mozilla::layers::BufferMode::BUFFER_NONE,
                                                   ScreenRotation(rotation));
        }
        return mLayerManager;
    }

    const nsTArray<nsWindow*>& windows = mScreen->GetTopWindows();
    nsWindow *topWindow = windows[0];

    if (!topWindow) {
        LOGW(" -- no topwindow\n");
        return nullptr;
    }

    CreateCompositor();
    if (mCompositorBridgeParent) {
        mScreen->SetCompositorBridgeParent(mCompositorBridgeParent);
        if (mScreen->IsPrimaryScreen()) {
            mComposer2D->SetCompositorBridgeParent(mCompositorBridgeParent);
        }
    }
    MOZ_ASSERT(mLayerManager);
    return mLayerManager;
}

void
nsWindow::DestroyCompositor()
{
    if (mCompositorBridgeParent) {
        mScreen->SetCompositorBridgeParent(nullptr);
        if (mScreen->IsPrimaryScreen()) {
            // Unset CompositorBridgeParent
            mComposer2D->SetCompositorBridgeParent(nullptr);
        }
    }
    nsBaseWidget::DestroyCompositor();
}

CompositorBridgeParent*
nsWindow::NewCompositorBridgeParent(int aSurfaceWidth, int aSurfaceHeight)
{
    return new CompositorBridgeParent(this, true, aSurfaceWidth, aSurfaceHeight);
}

void
nsWindow::BringToTop()
{
    const nsTArray<nsWindow*>& windows = mScreen->GetTopWindows();
    if (!windows.IsEmpty()) {
        if (nsIWidgetListener* listener = windows[0]->GetWidgetListener()) {
            listener->WindowDeactivated();
        }
    }

    mScreen->BringToTop(this);

    if (mWidgetListener) {
        mWidgetListener->WindowActivated();
    }

    Invalidate(mBounds);
}

void
nsWindow::UserActivity()
{
    if (!mIdleService) {
        mIdleService = do_GetService("@mozilla.org/widget/idleservice;1");
    }

    if (mIdleService) {
        mIdleService->ResetIdleTimeOut(0);
    }
}

uint32_t
nsWindow::GetGLFrameBufferFormat()
{
    if (mLayerManager &&
        mLayerManager->GetBackendType() == mozilla::layers::LayersBackend::LAYERS_OPENGL) {
        // We directly map the hardware fb on Gonk.  The hardware fb
        // has RGB format.
        return LOCAL_GL_RGB;
    }
    return LOCAL_GL_NONE;
}

LayoutDeviceIntRect
nsWindow::GetNaturalBounds()
{
    return mScreen->GetNaturalBounds();
}

nsScreenGonk*
nsWindow::GetScreen()
{
    return mScreen;
}

bool
nsWindow::NeedsPaint()
{
  if (!mLayerManager) {
    return false;
  }
  return nsIWidget::NeedsPaint();
}

Composer2D*
nsWindow::GetComposer2D()
{
    if (mScreen->GetDisplayType() == GonkDisplay::DISPLAY_VIRTUAL) {
        return nullptr;
    }

    return mComposer2D;
}
