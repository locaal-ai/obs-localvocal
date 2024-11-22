#pragma once
#include <string>
#include <memory>
#include <stdexcept>

// Custom exception
class TranslationError : public std::runtime_error {
public:
	explicit TranslationError(const std::string &message) : std::runtime_error(message) {}
};

// Abstract translator interface
class ITranslator {
public:
	virtual ~ITranslator() = default;

	virtual std::string translate(const std::string &text, const std::string &target_lang,
				      const std::string &source_lang = "auto") = 0;
};

// Factory function declaration
std::unique_ptr<ITranslator> createTranslator(const std::string &provider,
					      const std::string &api_key,
					      const std::string &location = "");

inline std::string sanitize_language_code(const std::string &lang_code)
{
	// Remove all non-alphabetic characters
	std::string sanitized_code;
	for (const char &c : lang_code) {
		if (isalpha((int)c)) {
			sanitized_code += c;
		}
	}
	return sanitized_code;
}
