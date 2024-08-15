#include "filter-replace-utils.h"

#include <nlohmann/json.hpp>

std::string serialize_filter_words_replace(
	const std::vector<std::tuple<std::string, std::string>> &filter_words_replace)
{
	if (filter_words_replace.empty()) {
		return "[]";
	}
	// use JSON to serialize the filter_words_replace map
	nlohmann::json j;
	for (const auto &entry : filter_words_replace) {
		j.push_back({{"key", std::get<0>(entry)}, {"value", std::get<1>(entry)}});
	}
	return j.dump();
}

std::vector<std::tuple<std::string, std::string>>
deserialize_filter_words_replace(const std::string &filter_words_replace_str)
{
	if (filter_words_replace_str.empty()) {
		return {};
	}
	// use JSON to deserialize the filter_words_replace map
	std::vector<std::tuple<std::string, std::string>> filter_words_replace;
	nlohmann::json j = nlohmann::json::parse(filter_words_replace_str);
	for (const auto &entry : j) {
		filter_words_replace.push_back(std::make_tuple(entry["key"], entry["value"]));
	}
	return filter_words_replace;
}
