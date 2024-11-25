#include "custom-api.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <regex>
#include <stdexcept>

using json = nlohmann::json;

CustomApiTranslator::CustomApiTranslator(const std::string &endpoint,
					 const std::string &body_template,
					 const std::string &response_json_path)
	: endpoint_(endpoint),
	  body_template_(body_template),
	  response_json_path_(response_json_path),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

CustomApiTranslator::~CustomApiTranslator() = default;

std::string CustomApiTranslator::translate(const std::string &text, const std::string &target_lang,
					   const std::string &source_lang)
{
	// first encode text to JSON compatible string
	nlohmann::json tmp = text;
	std::string textStr = tmp.dump();
	// remove '"' from the beginning and end of the string
	textStr = textStr.substr(1, textStr.size() - 2);
	// then replace the placeholders in the body template
	std::unordered_map<std::string, std::string> values = {
		{"\\{\\{sentence\\}\\}", textStr},
		{"\\{\\{target_lang\\}\\}", target_lang},
		{"\\{\\{source_lang\\}\\}", source_lang}};

	std::string body = replacePlaceholders(body_template_, values);
	std::string response;

	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
								 curl_easy_cleanup);

	if (!curl) {
		throw std::runtime_error("Failed to initialize CURL session");
	}

	try {
		// Set up curl options
		curl_easy_setopt(curl.get(), CURLOPT_URL, endpoint_.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlHelper::WriteCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);

		// Set up POST request
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());

		// Set up headers
		struct curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

		// Perform request
		CURLcode res = curl_easy_perform(curl.get());

		// Clean up headers
		curl_slist_free_all(headers);

		if (res != CURLE_OK) {
			throw TranslationError(std::string("CURL request failed: ") +
					       curl_easy_strerror(res));
		}

		return parseResponse(response);

	} catch (const std::exception &e) {
		throw TranslationError(std::string("JSON parsing error: ") + e.what());
	}
}

std::string CustomApiTranslator::replacePlaceholders(
	const std::string &template_str,
	const std::unordered_map<std::string, std::string> &values) const
{
	std::string result = template_str;
	for (const auto &pair : values) {
		try {
			std::regex placeholder(pair.first);
			result = std::regex_replace(result, placeholder, pair.second);
		} catch (const std::regex_error &e) {
			// Handle regex error
			throw TranslationError(std::string("Regex error: ") + e.what());
		}
	}
	return result;
}

std::string CustomApiTranslator::parseResponse(const std::string &response_str)
{
	try {
		// parse the JSON response
		json response = json::parse(response_str);

		// extract the translation from the JSON response
		std::string response_out = response[response_json_path_];

		return response_out;
	} catch (const json::exception &e) {
		throw TranslationError(std::string("JSON parsing error: ") + e.what());
	}
}
