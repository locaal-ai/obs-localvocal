#pragma once
#include "ITranslator.h"
#include <memory>
#include <string>
#include <map>

class CurlHelper; // Forward declaration

class AWSTranslator : public ITranslator {
public:
	AWSTranslator(const std::string &access_key, const std::string &secret_key,
		      const std::string &region = "us-east-1");
	~AWSTranslator() override;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	// AWS Signature V4 helper functions
	std::string createSigningKey(const std::string &date_stamp) const;
	std::string calculateSignature(const std::string &string_to_sign,
				       const std::string &signing_key) const;
	std::string getSignedHeaders(const std::map<std::string, std::string> &headers) const;
	std::string sha256(const std::string &str) const;
	std::string hmacSha256(const std::string &key, const std::string &data) const;

	// Response handling
	std::string parseResponse(const std::string &response_str);

	std::string access_key_;
	std::string secret_key_;
	std::string region_;
	std::unique_ptr<CurlHelper> curl_helper_;

	// AWS specific constants
	const std::string SERVICE_NAME = "translate";
	const std::string ALGORITHM = "AWS4-HMAC-SHA256";
};
