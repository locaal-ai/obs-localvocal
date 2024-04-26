#ifndef TRANSCRIPTION_UTILS_H
#define TRANSCRIPTION_UTILS_H

#include <string>
#include <vector>

std::string fix_utf8(const std::string &str);
std::string remove_leading_trailing_nonalpha(const std::string &str);
std::vector<std::string> split(const std::string &string, char delimiter);

#endif // TRANSCRIPTION_UTILS_H
