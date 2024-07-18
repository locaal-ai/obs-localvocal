#ifndef LANGUAGE_CODES_H
#define LANGUAGE_CODES_H

#include <map>
#include <string>

extern std::map<std::string, std::string> language_codes;
extern std::map<std::string, std::string> language_codes_reverse;
extern std::map<std::string, std::string> language_codes_from_whisper;
extern std::map<std::string, std::string> language_codes_to_whisper;

#endif // LANGUAGE_CODES_H
