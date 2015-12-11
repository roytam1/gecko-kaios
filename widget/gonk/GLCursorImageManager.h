/* Copyright (C) 2015 Acadine Technologies. All rights reserved. */

#ifndef GLCursorImageManager_h
#define GLCursorImageManager_h

#include <map>

#include "imgIRequest.h"
#include "imgINotificationObserver.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/ReentrantMonitor.h"
#include "nsWindow.h"

// Managers asynchronous loading of gl cursor image.
class GLCursorImageManager {
public:
  struct GLCursorImage {
    nsCursor mCursor;
    nsIntSize mImgSize;
    nsIntPoint mHotspot;
    RefPtr<mozilla::gfx::DataSourceSurface> mSurface;
  };

  GLCursorImageManager();

  // Called by nsWindow.
  // Prepare asynchronous load task for cursor if it doesn't exist.
  void PrepareCursorImage(nsCursor aCursor, nsWindow *aWindow);
  bool IsCursorImageReady(nsCursor aCursor);
  // Get the GLCursorImage corresponding to a cursor, sould be called after
  // checking by IsCursorImageReady().
  GLCursorImage GetGLCursorImage(nsCursor aCursor);

  // Called back by LoadCursorTask.
  void NotifyCursorImageLoadDone(nsCursor aCursor,
                                 GLCursorImage &GLCursorImage);
  // Called back by RemoveLoadCursorTaskOnMainThread.
  void RemoveCursorLoadRequest(nsCursor aCursor);

private:
  bool IsCursorImageLoading(nsCursor aCursor);

  class LoadCursorTask final : public imgINotificationObserver {
  public:
    NS_DECL_ISUPPORTS

    LoadCursorTask(nsCursor aCursor,
                   nsIntPoint aHotspot,
                   GLCursorImageManager *aManager);

    // This callback function will be called on main thread and notify the
    // status of image decoding process.
    NS_IMETHODIMP Notify(imgIRequest *aProxy,
                         int32_t aType,
                         const nsIntRect *aRect) override;
  private:
    ~LoadCursorTask();
    nsCursor mCursor;
    nsIntPoint mHotspot;
    GLCursorImageManager *mManager;
  };

  struct GLCursorLoadRequest {
    RefPtr<imgIRequest> mRequest;
    RefPtr<LoadCursorTask> mTask;
  };

  // Store cursors which are in loading process.
  std::map<nsCursor, GLCursorLoadRequest> mGLCursorLoadingRequestMap;

  // Store cursor images which are ready to render.
  std::map<nsCursor, GLCursorImage> mGLCursorImageMap;

  // Locks against both mGLCursorImageMap and mGLCursorLoadingRequestMap.
  mozilla::ReentrantMonitor mGLCursorImageManagerMonitor;
};

#endif /* GLCursorImageManager_h */
