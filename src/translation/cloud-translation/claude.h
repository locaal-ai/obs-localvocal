#pragma once
#include "ITranslator.h"
#include <memory>

class CurlHelper; // Forward declaration

class ClaudeTranslator : public ITranslator {
public:
	explicit ClaudeTranslator(const std::string &api_key,
				  const std::string &model = "claude-3-sonnet-20240229");
	~ClaudeTranslator() override;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	std::string parseResponse(const std::string &response_str);
	std::string createSystemPrompt(const std::string &target_lang) const;
	std::string getLanguageName(const std::string &lang_code) const;
	bool isLanguageSupported(const std::string &lang_code) const;

	std::string api_key_;
	std::string model_;
	std::unique_ptr<CurlHelper> curl_helper_;
};
