[Constructor]
interface Xt9Connect {
	[Throws]
	void setLetter (unsigned long hexPrefix, unsigned long hexLetter);

	readonly attribute DOMString wholeWord;

	readonly attribute DOMString candidateWord;

	readonly attribute short totalWord;

	readonly attribute long cursorPosition;
};