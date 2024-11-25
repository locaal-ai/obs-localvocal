#include "deepl.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

DeepLTranslator::DeepLTranslator(const std::string &api_key, bool free)
	: api_key_(api_key),
	  free_(free),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

DeepLTranslator::~DeepLTranslator() = default;

std::string DeepLTranslator::translate(const std::string &text, const std::string &target_lang,
				       const std::string &source_lang)
{
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
								 curl_easy_cleanup);

	if (!curl) {
		throw TranslationError("DeepL Failed to initialize CURL session");
	}

	std::string response;

	try {
		// Construct URL with parameters
		// Note: DeepL uses uppercase language codes
		std::string upperTarget = sanitize_language_code(target_lang);
		std::string upperSource = sanitize_language_code(source_lang);
		for (char &c : upperTarget)
			c = (char)std::toupper((int)c);
		for (char &c : upperSource)
			c = (char)std::toupper((int)c);

		json body = {{"text", {text}},
			     {"target_lang", upperTarget},
			     {"source_lang", upperSource}};
		const std::string body_str = body.dump();

		std::string url = "https://api.deepl.com/v2/translate";
		if (free_) {
			url = "https://api-free.deepl.com/v2/translate";
		}

		// Set up curl options
		curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlHelper::WriteCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body_str.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, body_str.size());
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);

		// DeepL requires specific headers
		struct curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers,
					    ("Authorization: DeepL-Auth-Key " + api_key_).c_str());
		curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

		CURLcode res = curl_easy_perform(curl.get());

		// Clean up headers
		curl_slist_free_all(headers);

		if (res != CURLE_OK) {
			throw TranslationError(std::string("DeepL: CURL request failed: ") +
					       curl_easy_strerror(res));
		}

		return parseResponse(response);

	} catch (const json::exception &e) {
		throw TranslationError(std::string("DeepL JSON parsing error: ") + e.what() +
				       ". Response: " + response);
	}
}

std::string DeepLTranslator::parseResponse(const std::string &response_str)
{
	// Handle rate limiting errors
	long response_code;
	curl_easy_getinfo(curl_easy_init(), CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code == 429) {
		throw TranslationError("DeepL API Error: Rate limit exceeded");
	}
	if (response_code == 456) {
		throw TranslationError("DeepL API Error: Quota exceeded");
	}

	/*
    {
        "translations": [
            {
            "detected_source_language": "EN",
            "text": "Hallo, Welt!"
            }
        ]
    }
    */
	json response = json::parse(response_str);

	// Check for API errors
	if (response.contains("message")) {
		throw TranslationError("DeepL API Error: " +
				       response["message"].get<std::string>());
	}

	try {
		// DeepL returns translations array with detected language
		const auto &translation = response["translations"][0];

		// Optionally, you can access the detected source language
		// if (translation.contains("detected_source_language")) {
		//     std::string detected = translation["detected_source_language"];
		// }

		return translation["text"].get<std::string>();
	} catch (const json::exception &) {
		throw TranslationError("DeepL: Unexpected response format from DeepL API");
	}
}
