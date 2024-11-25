#pragma once
#include "ITranslator.h"
#include <memory>

class CurlHelper; // Forward declaration

class DeepLTranslator : public ITranslator {
public:
	explicit DeepLTranslator(const std::string &api_key, bool free = false);
	~DeepLTranslator() override;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	std::string parseResponse(const std::string &response_str);

	std::string api_key_;
	bool free_;
	std::unique_ptr<CurlHelper> curl_helper_;
};
