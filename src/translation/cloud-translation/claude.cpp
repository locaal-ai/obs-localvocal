#include "claude.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "translation/language_codes.h"

using json = nlohmann::json;

ClaudeTranslator::ClaudeTranslator(const std::string &api_key, const std::string &model)
	: api_key_(api_key),
	  model_(model),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

ClaudeTranslator::~ClaudeTranslator() = default;

std::string ClaudeTranslator::createSystemPrompt(const std::string &target_lang) const
{
	std::string target_language = getLanguageName(target_lang);

	return "You are a professional translator. Translate the user's text into " +
	       target_language + " while preserving the meaning, tone, and style. " +
	       "Provide only the translated text without explanations, notes, or any other content. " +
	       "Maintain any formatting, line breaks, or special characters from the original text.";
}

std::string ClaudeTranslator::translate(const std::string &text, const std::string &target_lang,
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
		std::string url = "https://api.anthropic.com/v1/messages";

		// Create request body
		json request_body = {{"model", model_},
				     {"max_tokens", 4096},
				     {"system", createSystemPrompt(target_lang)},
				     {"messages",
				      json::array({{{"role", "user"}, {"content", text}}})}};

		if (source_lang != "auto") {
			request_body["system"] = createSystemPrompt(target_lang) +
						 " The source text is in " +
						 getLanguageName(source_lang) + ".";
		}

		std::string payload = request_body.dump();

		// Set up headers
		struct curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers, ("x-api-key: " + api_key_).c_str());
		headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

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

std::string ClaudeTranslator::parseResponse(const std::string &response_str)
{
	try {
		json response = json::parse(response_str);

		if (!response.contains("content") || !response["content"].is_array() ||
		    response["content"].empty() || !response["content"][0].contains("text")) {
			throw TranslationError("Invalid response format from Claude API");
		}

		return response["content"][0]["text"].get<std::string>();

	} catch (const json::exception &e) {
		throw TranslationError(std::string("Failed to parse Claude response: ") + e.what());
	}
}
