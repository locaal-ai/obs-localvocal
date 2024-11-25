#pragma once
#include "ITranslator.h"
#include <memory>

class CurlHelper; // Forward declaration

class OpenAITranslator : public ITranslator {
public:
	explicit OpenAITranslator(const std::string &api_key,
				  const std::string &model = "gpt-4-turbo-preview");
	~OpenAITranslator() override;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	std::string parseResponse(const std::string &response_str);
	std::string createSystemPrompt(const std::string &target_lang) const;

	std::string api_key_;
	std::string model_;
	std::unique_ptr<CurlHelper> curl_helper_;
};
