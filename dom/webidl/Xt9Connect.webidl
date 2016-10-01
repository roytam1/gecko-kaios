/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

[Constructor,
Constructor(unsigned long et9_lid)]
interface Xt9Connect {
  [Throws]
  void setLetter (unsigned long hexPrefix, unsigned long hexLetter);

  unsigned long setLanguage(unsigned long et9_lid);

  attribute boolean initEmptyWord;

  attribute DOMString wholeWord;

  readonly attribute DOMString candidateWord;

  readonly attribute short totalWord;

  attribute long cursorPosition;

  readonly attribute unsigned long currentEt9LID;
};