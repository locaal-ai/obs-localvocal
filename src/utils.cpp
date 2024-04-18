#include "utils.h"

std::vector<std::string> split_words(const std::string &str_copy)
{
	std::vector<std::string> words;
	std::string word;
	for (char c : str_copy) {
		if (std::isspace(c)) {
			if (!word.empty()) {
				words.push_back(word);
				word.clear();
			}
		} else {
			word += c;
		}
	}
	if (!word.empty()) {
		words.push_back(word);
	}
	return words;
}
