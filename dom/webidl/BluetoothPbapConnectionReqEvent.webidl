/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright (C) 2016 Acadine Technologies. All rights reserved.
 */

[CheckAnyPermissions="bluetooth",
 Constructor(DOMString type,
             optional BluetoothPbapConnectionReqEventInit eventInitDict)]
interface BluetoothPbapConnectionReqEvent : Event
{
  readonly attribute DOMString                  address;
  readonly attribute BluetoothConnectionHandle? handle;
};

dictionary BluetoothPbapConnectionReqEventInit : EventInit
{
  DOMString                  address = "";
  BluetoothConnectionHandle? handle = null;
};
