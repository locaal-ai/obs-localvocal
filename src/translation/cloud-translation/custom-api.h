#pragma once
#include "ITranslator.h"
#include <memory>
#include <string>
#include <unordered_map>

class CurlHelper; // Forward declaration

class CustomApiTranslator : public ITranslator {
public:
	explicit CustomApiTranslator(const std::string &endpoint, const std::string &body_template,
				     const std::string &response_json_path);
	~CustomApiTranslator() override;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	std::string
	replacePlaceholders(const std::string &template_str,
			    const std::unordered_map<std::string, std::string> &values) const;
	std::string parseResponse(const std::string &response_str);

	std::string endpoint_;
	std::string body_template_;
	std::string response_json_path_;
	std::unique_ptr<CurlHelper> curl_helper_;
};
