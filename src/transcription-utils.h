#ifndef TRANSCRIPTION_UTILS_H
#define TRANSCRIPTION_UTILS_H

#include <string>

std::string fix_utf8(const std::string &str);
std::string remove_leading_trailing_nonalpha(const std::string &str);

#endif // TRANSCRIPTION_UTILS_H
