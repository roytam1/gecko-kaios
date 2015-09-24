[Constructor]
interface Xt9Connect {
	[Throws]
	void setLetter (unsigned long hexPrefix, unsigned long hexLetter);

	attribute boolean initEmptyWord;

	attribute DOMString wholeWord;

	readonly attribute DOMString candidateWord;

	readonly attribute short totalWord;

	attribute long cursorPosition;
};