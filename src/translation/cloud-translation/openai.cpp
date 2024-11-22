#include "openai.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "translation/language_codes.h"

using json = nlohmann::json;

OpenAITranslator::OpenAITranslator(const std::string &api_key, const std::string &model)
	: api_key_(api_key),
	  model_(model),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

OpenAITranslator::~OpenAITranslator() = default;

std::string OpenAITranslator::createSystemPrompt(const std::string &target_lang) const
{
	std::string target_language = getLanguageName(target_lang);

	return "You are a professional translator. Translate the user's text into " +
	       target_language + ". Maintain the exact meaning, tone, and style. " +
	       "Respond with only the translated text, without any explanations or additional content. " +
	       "Preserve all formatting, line breaks, and special characters from the original text.";
}

std::string OpenAITranslator::translate(const std::string &text, const std::string &target_lang,
					const std::string &source_lang)
{
	if (!isLanguageSupported(target_lang)) {
		throw TranslationError("Unsupported target language: " + target_lang);
	}

	if (source_lang != "auto" && !isLanguageSupported(source_lang)) {
		throw TranslationError("Unsupported source language: " + source_lang);
	}

	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
								 curl_easy_cleanup);

	if (!curl) {
		throw TranslationError("Failed to initialize CURL session");
	}

	std::string response;

	try {
		// Prepare the request
		std::string url = "https://api.openai.com/v1/chat/completions";

		// Create messages array
		json messages = json::array();

		// Add system message
		messages.push_back(
			{{"role", "system"}, {"content", createSystemPrompt(target_lang)}});

		// Add user message with source language if specified
		std::string user_prompt = text;
		if (source_lang != "auto") {
			user_prompt = "Translate the following " + getLanguageName(source_lang) +
				      " text:\n\n" + text;
		}

		messages.push_back({{"role", "user"}, {"content", user_prompt}});

		// Create request body
		json request_body = {{"model", model_},
				     {"messages", messages},
				     {"temperature",
				      0.3}, // Lower temperature for more consistent translations
				     {"max_tokens", 4000}};

		std::string payload = request_body.dump();

		// Set up headers
		struct curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());

		// Set up CURL request
		curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, payload.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlHelper::WriteCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);

		// Perform request
		CURLcode res = curl_easy_perform(curl.get());

		// Clean up
		curl_slist_free_all(headers);

		if (res != CURLE_OK) {
			throw TranslationError(std::string("CURL request failed: ") +
					       curl_easy_strerror(res));
		}

		// Check HTTP response code
		long response_code;
		curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);

		if (response_code != 200) {
			throw TranslationError("HTTP error: " + std::to_string(response_code) +
					       "\nResponse: " + response);
		}

		return parseResponse(response);

	} catch (const json::exception &e) {
		throw TranslationError(std::string("JSON parsing error: ") + e.what());
	}
}

std::string OpenAITranslator::parseResponse(const std::string &response_str)
{
	try {
		json response = json::parse(response_str);

		if (!response.contains("choices") || response["choices"].empty() ||
		    !response["choices"][0].contains("message") ||
		    !response["choices"][0]["message"].contains("content")) {
			throw TranslationError("Invalid response format from OpenAI API");
		}

		return response["choices"][0]["message"]["content"].get<std::string>();

	} catch (const json::exception &e) {
		throw TranslationError(std::string("Failed to parse OpenAI response: ") + e.what());
	}
}
