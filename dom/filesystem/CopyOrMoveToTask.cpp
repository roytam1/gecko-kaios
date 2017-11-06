/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CopyOrMoveToTask.h"

#include "mozilla/dom/File.h"
#include "mozilla/dom/FileSystemBase.h"
#include "mozilla/dom/FileSystemUtils.h"
#include "mozilla/dom/PFileSystemParams.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ipc/BlobChild.h"
#include "mozilla/dom/ipc/BlobParent.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsIFile.h"
#include "nsStringGlue.h"

namespace mozilla {
namespace dom {

/**
 * CopyOrMoveToTaskChild
 */

/* static */ already_AddRefed<CopyOrMoveToTaskChild>
CopyOrMoveToTaskChild::Create(FileSystemBase* aFileSystem,
                              nsIFile* aDirPath,
                              nsIFile* aSrcPath,
                              nsIFile* aDstPath,
                              bool aIsCopy,
                              ErrorResult& aRv)

{
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread!");
  MOZ_ASSERT(aFileSystem);
  MOZ_ASSERT(aDirPath);
  MOZ_ASSERT(aSrcPath);
  MOZ_ASSERT(aDstPath);

  RefPtr<CopyOrMoveToTaskChild> task =
    new CopyOrMoveToTaskChild(aFileSystem, aDirPath, aSrcPath, aDstPath, aIsCopy);

  nsCOMPtr<nsIGlobalObject> globalObject =
    do_QueryInterface(aFileSystem->GetParentObject());
  if (NS_WARN_IF(!globalObject)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  task->mPromise = Promise::Create(globalObject, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return task.forget();
}

CopyOrMoveToTaskChild::CopyOrMoveToTaskChild(FileSystemBase* aFileSystem,
                                            nsIFile* aDirPath,
                                            nsIFile* aSrcPath,
                                            nsIFile* aDstPath,
                                            bool aIsCopy)
  : FileSystemTaskChildBase(aFileSystem)
  , mDirPath(aDirPath)
  , mSrcPath(aSrcPath)
  , mDstPath(aDstPath)
  , mIsCopy(aIsCopy)
  , mReturnValue(false)
{
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread!");
  MOZ_ASSERT(aFileSystem);
  MOZ_ASSERT(aDirPath);
  MOZ_ASSERT(aSrcPath);
  MOZ_ASSERT(aDstPath);
}

CopyOrMoveToTaskChild::~CopyOrMoveToTaskChild()
{
  MOZ_ASSERT(NS_IsMainThread());
}

already_AddRefed<Promise>
CopyOrMoveToTaskChild::GetPromise()
{
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread!");
  return RefPtr<Promise>(mPromise).forget();
}

FileSystemParams
CopyOrMoveToTaskChild::GetRequestParams(const nsString& aSerializedDOMPath,
                                        ErrorResult& aRv) const
{
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread!");
  FileSystemCopyOrMoveToParams param;
  param.filesystem() = aSerializedDOMPath;

  aRv = mDirPath->GetPath(param.directory());
  if (NS_WARN_IF(aRv.Failed())) {
    return param;
  }

  param.isCopy() = mIsCopy;

  nsAutoString srcPath;
  aRv = mSrcPath->GetPath(srcPath);
  if (NS_WARN_IF(aRv.Failed())) {
    return param;
  }
  param.srcRealPath() = srcPath;

  nsAutoString dstPath;
  aRv = mDstPath->GetPath(dstPath);
  if (NS_WARN_IF(aRv.Failed())) {
    return param;
  }
  param.dstRealPath() = dstPath;


  return param;
}

void
CopyOrMoveToTaskChild::SetSuccessRequestResult(
      const FileSystemResponseValue& aValue,
      ErrorResult& aRv)
{
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread!");

  FileSystemBooleanResponse r = aValue;
  mReturnValue = r.success();
}

void
CopyOrMoveToTaskChild::HandlerCallback()
{
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread!");

  if (mFileSystem->IsShutdown()) {
    mPromise = nullptr;
    return;
  }

  if (HasError()) {
    mPromise->MaybeReject(mErrorValue);
    mPromise = nullptr;
    return;
  }

  mPromise->MaybeResolve(mReturnValue);
  mPromise = nullptr;
}

void
CopyOrMoveToTaskChild::GetPermissionAccessType(nsCString& aAccess) const
{
  aAccess.AssignLiteral(DIRECTORY_WRITE_PERMISSION);
}

/**
 * CopyOrMoveToTaskParent
 */

/* static */ already_AddRefed<CopyOrMoveToTaskParent>
CopyOrMoveToTaskParent::Create(FileSystemBase* aFileSystem,
                              const FileSystemCopyOrMoveToParams& aParam,
                              FileSystemRequestParent* aParent,
                              ErrorResult& aRv)
{
  MOZ_ASSERT(XRE_IsParentProcess(), "Only call from parent process!");
  mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aFileSystem);

  RefPtr<CopyOrMoveToTaskParent> task =
    new CopyOrMoveToTaskParent(aFileSystem, aParam, aParent);

  NS_ConvertUTF16toUTF8 directoryPath(aParam.directory());
  aRv = NS_NewNativeLocalFile(directoryPath, true,
                              getter_AddRefs(task->mDirPath));
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  task->mIsCopy = aParam.isCopy();

  NS_ConvertUTF16toUTF8 srcPath(aParam.srcRealPath());
  aRv = NS_NewNativeLocalFile(srcPath, true, getter_AddRefs(task->mSrcPath));
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  NS_ConvertUTF16toUTF8 dstPath(aParam.dstRealPath());
  aRv = NS_NewNativeLocalFile(dstPath, true, getter_AddRefs(task->mDstPath));
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (!FileSystemUtils::IsDescendantPath(task->mDirPath, task->mSrcPath) ||
      !FileSystemUtils::IsDescendantPath(task->mDirPath, task->mDstPath) ) {
    aRv.Throw(NS_ERROR_DOM_FILESYSTEM_NO_MODIFICATION_ALLOWED_ERR);
    return nullptr;
  }

  return task.forget();
}

CopyOrMoveToTaskParent::CopyOrMoveToTaskParent(
        FileSystemBase* aFileSystem,
        const FileSystemCopyOrMoveToParams& aParam,
        FileSystemRequestParent* aParent)
  : FileSystemTaskParentBase(aFileSystem, aParam, aParent)
  , mIsCopy(false)
  , mReturnValue(false)
{
  MOZ_ASSERT(XRE_IsParentProcess(), "Only call from parent process!");
  mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aFileSystem);
}

FileSystemResponseValue
CopyOrMoveToTaskParent::GetSuccessRequestResult(ErrorResult& aRv) const
{
  mozilla::ipc::AssertIsOnBackgroundThread();

  return FileSystemBooleanResponse(mReturnValue);
}

nsresult
CopyOrMoveToTaskParent::IOWork()
{
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Only call from parent process!");
  MOZ_ASSERT(!NS_IsMainThread(), "Only call on worker thread!");

  if (mFileSystem->IsShutdown()) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(FileSystemUtils::IsDescendantPath(mDirPath, mSrcPath));
  MOZ_ASSERT(FileSystemUtils::IsDescendantPath(mDirPath, mDstPath));

  nsString fileName;

  bool exists = false;
  nsresult rv = mSrcPath->Exists(&exists);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!exists) {
    mReturnValue = false;
    return NS_OK;
  }

  bool isFile = false;
  rv = mSrcPath->IsFile(&isFile);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (isFile && !mFileSystem->IsSafeFile(mSrcPath)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  rv = mDstPath->Exists(&exists);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!exists) {
    mReturnValue = false;
    return NS_OK;
  }

  rv = mDstPath->IsFile(&isFile);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (isFile) {
    return NS_ERROR_DOM_FILESYSTEM_NO_MODIFICATION_ALLOWED_ERR;
  }

  rv = mSrcPath->GetLeafName(fileName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (mIsCopy) {
    rv = mSrcPath->CopyTo(mDstPath, fileName);
  } else {
    rv = mSrcPath->MoveTo(mDstPath, fileName);
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mReturnValue = true;
  return NS_OK;
}

void
CopyOrMoveToTaskParent::GetPermissionAccessType(nsCString& aAccess) const
{
  aAccess.AssignLiteral(DIRECTORY_WRITE_PERMISSION);
}

} // namespace dom
} // namespace mozilla