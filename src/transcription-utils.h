#ifndef TRANSCRIPTION_UTILS_H
#define TRANSCRIPTION_UTILS_H

#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cctype>

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

// Get the current timestamp in nano seconds since epoch
inline uint64_t now_ns()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		       std::chrono::system_clock::now().time_since_epoch())
		.count();
}

/**
 * @brief Calculates the elapsed time in nanoseconds since a given start time.
 *
 * This function takes a starting time in nanoseconds and returns the 
 * difference between the current time and the starting time.
 *
 * @param start_ns The starting time in nanoseconds.
 * @return The elapsed time in nanoseconds since the start time.
 */
inline uint64_t ns_since(uint64_t start_ns)
{
	return now_ns() - start_ns;
}

/**
 * @brief Calculates the elapsed time in milliseconds since a given start time.
 *
 * This function takes a start time in milliseconds and returns the difference
 * between the current time (in milliseconds) and the start time.
 *
 * @param start_ms The start time in milliseconds.
 * @return The elapsed time in milliseconds since the start time.
 */
inline uint64_t ms_since(uint64_t start_ms)
{
	return now_ms() - start_ms;
}

// Split a string into words based on spaces
std::vector<std::string> split_words(const std::string &str_copy);

// trim (strip) string from leading and trailing whitespaces
template<typename StringLike> StringLike trim(const StringLike &str)
{
	StringLike str_copy = str;
	str_copy.erase(str_copy.begin(),
		       std::find_if(str_copy.begin(), str_copy.end(),
				    [](unsigned char ch) { return !std::isspace(ch); }));
	str_copy.erase(std::find_if(str_copy.rbegin(), str_copy.rend(),
				    [](unsigned char ch) { return !std::isspace(ch); })
			       .base(),
		       str_copy.end());
	return str_copy;
}

#endif // TRANSCRIPTION_UTILS_H
