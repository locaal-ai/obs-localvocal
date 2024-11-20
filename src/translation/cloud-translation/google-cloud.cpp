#include "google-cloud.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

GoogleTranslator::GoogleTranslator(const std::string &api_key)
	: api_key_(api_key),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

GoogleTranslator::~GoogleTranslator() = default;

std::string GoogleTranslator::translate(const std::string &text, const std::string &target_lang,
					const std::string &source_lang)
{
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
								 curl_easy_cleanup);

	if (!curl) {
		throw TranslationError("Failed to initialize CURL session");
	}

	std::string response;

	try {
		// Construct URL with parameters
		std::stringstream url;
		url << "https://translation.googleapis.com/language/translate/v2"
		    << "?key=" << api_key_ << "&q=" << CurlHelper::urlEncode(curl.get(), text)
		    << "&target=" << target_lang;

		if (source_lang != "auto") {
			url << "&source=" << source_lang;
		}

		// Set up curl options
		curl_easy_setopt(curl.get(), CURLOPT_URL, url.str().c_str());
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlHelper::WriteCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);

		CURLcode res = curl_easy_perform(curl.get());

		if (res != CURLE_OK) {
			throw TranslationError(std::string("CURL request failed: ") +
					       curl_easy_strerror(res));
		}

		return parseResponse(response);

	} catch (const json::exception &e) {
		throw TranslationError(std::string("JSON parsing error: ") + e.what());
	}
}

std::string GoogleTranslator::parseResponse(const std::string &response_str)
{
	json response = json::parse(response_str);

	if (response.contains("error")) {
		const auto &error = response["error"];
		std::stringstream error_msg;
		error_msg << "Google API Error: ";
		if (error.contains("message")) {
			error_msg << error["message"].get<std::string>();
		}
		if (error.contains("code")) {
			error_msg << " (Code: " << error["code"].get<int>() << ")";
		}
		throw TranslationError(error_msg.str());
	}

	return response["data"]["translations"][0]["translatedText"].get<std::string>();
}
