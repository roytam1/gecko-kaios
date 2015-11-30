/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright (C) 2016 Acadine Technologies. All rights reserved.
 */

[CheckAnyPermissions="bluetooth",
 Constructor(DOMString type,
             optional BluetoothMapConnectionReqEventInit eventInitDict)]
interface BluetoothMapConnectionReqEvent : Event
{
  readonly attribute DOMString                  address;
  readonly attribute BluetoothConnectionHandle? handle;
};

dictionary BluetoothMapConnectionReqEventInit : EventInit
{
  DOMString                  address = "";
  BluetoothConnectionHandle? handle = null;
};
