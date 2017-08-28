/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NetIdManager.h"

#define DEBUG 0
#if DEBUG
#define NT_DBG(args...)  __android_log_print(ANDROID_LOG_DEBUG, "NetIdManager" , ## args)
#else
#define NT_DBG(args...)
#endif

#define GET_STR(param) NS_ConvertUTF16toUTF8(param).get()

NetIdManager::NetIdManager()
  : mNextNetId(MIN_NET_ID)
{
}

int NetIdManager::getNextNetId()
{
  // Modified from
  // http://androidxref.com/5.0.0_r2/xref/frameworks/base/services/
  //        core/java/com/android/server/ConnectivityService.java#764

  int netId = mNextNetId;
  if (++mNextNetId > MAX_NET_ID) {
    mNextNetId = MIN_NET_ID;
  }

  return netId;
}

void NetIdManager::acquire(const nsString& aInterfaceName,
                           NetIdInfo* aNetIdInfo, int aType)
{
  // Lookup or create one.
  if (!mInterfaceToNetIdHash.Get(aInterfaceName, aNetIdInfo)) {
    aNetIdInfo->mNetId = getNextNetId();
    aNetIdInfo->mTypes = 0;
  }

  NT_DBG("acquire: (%s/%d)",GET_STR(aInterfaceName),aType);
  add_type(aNetIdInfo->mTypes, aType);

  // Update hash and return.
  mInterfaceToNetIdHash.Put(aInterfaceName, *aNetIdInfo);

  return;
}

bool NetIdManager::lookup(const nsString& aInterfaceName,
                          NetIdInfo* aNetIdInfo)
{
  return mInterfaceToNetIdHash.Get(aInterfaceName, aNetIdInfo);
}

bool NetIdManager::release(const nsString& aInterfaceName,
                           NetIdInfo* aNetIdInfo, int aType)
{
  if (!mInterfaceToNetIdHash.Get(aInterfaceName, aNetIdInfo)) {
    return false; // No such key.
  }

  NT_DBG("release: (%s/%d)",GET_STR(aInterfaceName),aType);
  remove_type(aNetIdInfo->mTypes, aType);

  // Update the hash if still be referenced.
  if (aNetIdInfo->mTypes != 0){
    mInterfaceToNetIdHash.Put(aInterfaceName, *aNetIdInfo);
    return true;
  }

  // No longer be referenced. Remove the entry.
  mInterfaceToNetIdHash.Remove(aInterfaceName);

  return true;
}

/**
 * using for adding the network type in bitmask
 *
 * Refer to nsINetworkInfo interface
 *  NETWORK_TYPE_MOBILE(1) => 10
 *  NETWORK_TYPE_MOBILE_MMS(2) => 100
 *  NETWORK_TYPE_MOBILE_SUPL(3) => 1000
 *
 *  @param aTypes : current total network types
 *         type : the network type need to acquire
**/
void NetIdManager::add_type(NetType& aTypes, int type)
{
  aTypes = aTypes | (0x01 << type);
  NT_DBG("%s: %d",__FUNCTION__,aTypes);
}

/**
 * using for removing the network type in bitmask
 *
 * Refer to nsINetworkInfo interface
 *  NETWORK_TYPE_MOBILE(1) => 10
 *  NETWORK_TYPE_MOBILE_MMS(2) => 100
 *  NETWORK_TYPE_MOBILE_SUPL(3) => 1000
 *
 *  @param aTypes : current total network types
 *         type : the network type need to release
**/
void NetIdManager::remove_type(NetType& aTypes, int type)
{
  aTypes = aTypes ^ (0x01 << type);
  NT_DBG("%s: %d",__FUNCTION__,aTypes);
}
