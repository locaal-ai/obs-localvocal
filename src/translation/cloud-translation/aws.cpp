#include "aws.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <chrono>
#include <ctime>

using json = nlohmann::json;

AWSTranslator::AWSTranslator(const std::string &access_key, const std::string &secret_key,
			     const std::string &region)
	: access_key_(access_key),
	  secret_key_(secret_key),
	  region_(region),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

AWSTranslator::~AWSTranslator() = default;

// Helper function for SHA256 hashing
std::string AWSTranslator::sha256(const std::string &str) const
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, str.c_str(), str.size());
	SHA256_Final(hash, &sha256);

	std::stringstream ss;
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
	}
	return ss.str();
}

// Helper function for HMAC-SHA256
std::string AWSTranslator::hmacSha256(const std::string &key, const std::string &data) const
{
	unsigned char *digest = HMAC(EVP_sha256(), key.c_str(), key.length(),
				     (unsigned char *)data.c_str(), data.length(), nullptr,
				     nullptr);

	char hex[SHA256_DIGEST_LENGTH * 2 + 1];
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		sprintf(&hex[i * 2], "%02x", digest[i]);
	}
	return std::string(hex, SHA256_DIGEST_LENGTH * 2);
}

// Create AWS Signature Version 4 signing key
std::string AWSTranslator::createSigningKey(const std::string &date_stamp) const
{
	std::string k_date = hmacSha256("AWS4" + secret_key_, date_stamp);
	std::string k_region = hmacSha256(k_date, region_);
	std::string k_service = hmacSha256(k_region, SERVICE_NAME);
	return hmacSha256(k_service, "aws4_request");
}

std::string AWSTranslator::calculateSignature(const std::string &string_to_sign,
					      const std::string &signing_key) const
{
	return hmacSha256(signing_key, string_to_sign);
}

std::string AWSTranslator::getSignedHeaders(const std::map<std::string, std::string> &headers) const
{
	std::stringstream ss;
	for (auto it = headers.begin(); it != headers.end(); ++it) {
		if (it != headers.begin())
			ss << ";";
		ss << it->first;
	}
	return ss.str();
}

std::string AWSTranslator::translate(const std::string &text, const std::string &target_lang,
				     const std::string &source_lang)
{
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
								 curl_easy_cleanup);

	if (!curl) {
		throw TranslationError("Failed to initialize CURL session");
	}

	std::string response;

	try {
		// Create request body
		json request_body = {{"Text", text},
				     {"TargetLanguageCode", target_lang},
				     {"SourceLanguageCode",
				      source_lang == "auto" ? "auto" : source_lang}};
		std::string payload = request_body.dump();

		// Get current timestamp
		auto now = std::chrono::system_clock::now();
		auto time_t_now = std::chrono::system_clock::to_time_t(now);

		char amz_date[17];
		char date_stamp[9];
		strftime(amz_date, sizeof(amz_date), "%Y%m%dT%H%M%SZ", gmtime(&time_t_now));
		strftime(date_stamp, sizeof(date_stamp), "%Y%m%d", gmtime(&time_t_now));

		// Create canonical request headers
		std::map<std::string, std::string> headers;
		headers["content-type"] = "application/json";
		headers["host"] = "translate." + region_ + ".amazonaws.com";
		headers["x-amz-content-sha256"] = sha256(payload);
		headers["x-amz-date"] = amz_date;

		// Create canonical request
		std::stringstream canonical_request;
		canonical_request << "POST\n"
				  << "/\n" // canonical URI
				  << "\n"  // canonical query string (empty)
				  << "content-type:" << headers["content-type"] << "\n"
				  << "host:" << headers["host"] << "\n"
				  << "x-amz-content-sha256:" << headers["x-amz-content-sha256"]
				  << "\n"
				  << "x-amz-date:" << headers["x-amz-date"] << "\n"
				  << "\n" // end of headers
				  << getSignedHeaders(headers) << "\n"
				  << sha256(payload);

		// Create string to sign
		std::stringstream string_to_sign;
		string_to_sign << ALGORITHM << "\n"
			       << amz_date << "\n"
			       << date_stamp << "/" << region_ << "/" << SERVICE_NAME
			       << "/aws4_request\n"
			       << sha256(canonical_request.str());

		// Calculate signature
		std::string signing_key = createSigningKey(date_stamp);
		std::string signature = calculateSignature(string_to_sign.str(), signing_key);

		// Create Authorization header
		std::stringstream auth_header;
		auth_header << ALGORITHM << " Credential=" << access_key_ << "/" << date_stamp
			    << "/" << region_ << "/" << SERVICE_NAME << "/aws4_request,"
			    << "SignedHeaders=" << getSignedHeaders(headers) << ","
			    << "Signature=" << signature;

		// Set up CURL request
		std::string url = "https://" + headers["host"] + "/";

		curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, payload.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlHelper::WriteCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

		// Set headers
		struct curl_slist *header_list = nullptr;
		header_list = curl_slist_append(
			header_list, ("Content-Type: " + headers["content-type"]).c_str());
		header_list = curl_slist_append(header_list,
						("X-Amz-Date: " + headers["x-amz-date"]).c_str());
		header_list = curl_slist_append(
			header_list,
			("X-Amz-Content-Sha256: " + headers["x-amz-content-sha256"]).c_str());
		header_list = curl_slist_append(header_list,
						("Authorization: " + auth_header.str()).c_str());

		curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header_list);

		// Perform request
		CURLcode res = curl_easy_perform(curl.get());

		// Clean up
		curl_slist_free_all(header_list);

		if (res != CURLE_OK) {
			throw TranslationError(std::string("CURL request failed: ") +
					       curl_easy_strerror(res));
		}

		return parseResponse(response);

	} catch (const json::exception &e) {
		throw TranslationError(std::string("JSON parsing error: ") + e.what());
	}
}

std::string AWSTranslator::parseResponse(const std::string &response_str)
{
	try {
		json response = json::parse(response_str);

		// Check for error response
		if (response.contains("__type")) {
			throw TranslationError("AWS API Error: " +
					       response.value("message", "Unknown error"));
		}

		return response["TranslatedText"].get<std::string>();

	} catch (const json::exception &e) {
		throw TranslationError(std::string("Failed to parse AWS response: ") + e.what());
	}
}
