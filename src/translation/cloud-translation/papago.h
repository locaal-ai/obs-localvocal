#pragma once
#include "ITranslator.h"
#include <memory>

class CurlHelper; // Forward declaration

class PapagoTranslator : public ITranslator {
public:
	PapagoTranslator(const std::string &client_id, const std::string &client_secret);
	~PapagoTranslator() override;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	std::string parseResponse(const std::string &response_str);
	std::string mapLanguageCode(const std::string &lang_code) const;
	bool isLanguagePairSupported(const std::string &source, const std::string &target) const;

	std::string client_id_;
	std::string client_secret_;
	std::unique_ptr<CurlHelper> curl_helper_;
};
