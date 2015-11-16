/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright (C) 2016 Acadine Technologies. All rights reserved.
 */

[CheckAnyPermissions="bluetooth",
 Constructor(DOMString type,
             optional BluetoothConnectionReqEventInit eventInitDict)]
interface BluetoothConnectionReqEvent : Event
{
  readonly attribute DOMString                  address;
  readonly attribute unsigned short             serviceUuid;
  readonly attribute BluetoothConnectionHandle? handle;
};

dictionary BluetoothConnectionReqEventInit : EventInit
{
  DOMString                  address = "";
  unsigned short             serviceUuid = 0;
  BluetoothConnectionHandle? handle = null;
};
