#ifndef FILTER_REPLACE_UTILS_H
#define FILTER_REPLACE_UTILS_H

#include <string>
#include <vector>

std::string serialize_filter_words_replace(
	const std::vector<std::tuple<std::string, std::string>> &filter_words_replace);
std::vector<std::tuple<std::string, std::string>>
deserialize_filter_words_replace(const std::string &filter_words_replace_str);

#endif /* FILTER_REPLACE_UTILS_H */