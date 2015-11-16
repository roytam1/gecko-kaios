/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright (C) 2016 Acadine Technologies. All rights reserved.
 */

[CheckAnyPermissions="bluetooth"]
interface BluetoothConnectionHandle
{
  /**
   * Accept the bluetooth profile connection request.
   */
  [NewObject, Throws]
  DOMRequest accept();

  /**
   * Reject the bluetooth profile connection request.
   */
  [NewObject, Throws]
  DOMRequest reject();
};
