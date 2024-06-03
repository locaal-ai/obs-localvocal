#ifndef TRANSCRIPTION_UTILS_H
#define TRANSCRIPTION_UTILS_H

#include <string>
#include <vector>
#include <chrono>

// Fix UTF8 string for Windows
std::string fix_utf8(const std::string &str);

// Remove leading and trailing non-alphabetic characters
std::string remove_leading_trailing_nonalpha(const std::string &str);

// Split a string by a delimiter
std::vector<std::string> split(const std::string &string, char delimiter);

// Get the current timestamp in milliseconds since epoch
inline uint64_t now_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		       std::chrono::system_clock::now().time_since_epoch())
		.count();
}

// Split a string into words based on spaces
std::vector<std::string> split_words(const std::string &str_copy);

#endif // TRANSCRIPTION_UTILS_H
