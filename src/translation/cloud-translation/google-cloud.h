#pragma once
#include "ITranslator.h"
#include <memory>

class CurlHelper; // Forward declaration

class GoogleTranslator : public ITranslator {
public:
	explicit GoogleTranslator(const std::string &api_key);
	~GoogleTranslator() override;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	std::string parseResponse(const std::string &response_str);

	std::string api_key_;
	std::unique_ptr<CurlHelper> curl_helper_;
};
