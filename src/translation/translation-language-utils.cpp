#include "translation-language-utils.h"

#include <unicode/unistr.h>
#include <unicode/uchar.h>

std::string remove_start_punctuation(const std::string &text)
{
	if (text.empty()) {
		return text;
	}

	// Convert the input string to ICU's UnicodeString
	icu::UnicodeString ustr = icu::UnicodeString::fromUTF8(text);

	// Find the index of the first non-punctuation character
	int32_t start = 0;
	while (start < ustr.length()) {
		UChar32 ch = ustr.char32At(start);
		if (!u_ispunct(ch)) {
			break;
		}
		start += U16_LENGTH(ch);
	}

	// Create a new UnicodeString with punctuation removed from the start
	icu::UnicodeString result = ustr.tempSubString(start);

	// Convert the result back to UTF-8
	std::string output;
	result.toUTF8String(output);

	return output;
}
