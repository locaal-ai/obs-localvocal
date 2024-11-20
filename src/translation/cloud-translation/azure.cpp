#include "azure.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

AzureTranslator::AzureTranslator(const std::string &api_key, const std::string &location,
				 const std::string &endpoint)
	: api_key_(api_key),
	  location_(location),
	  endpoint_(endpoint),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

AzureTranslator::~AzureTranslator() = default;

std::string AzureTranslator::translate(const std::string &text, const std::string &target_lang,
				       const std::string &source_lang)
{
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
								 curl_easy_cleanup);

	if (!curl) {
		throw TranslationError("Failed to initialize CURL session");
	}

	std::string response;

	try {
		// Construct the route
		std::stringstream route;
		route << "/translate?api-version=3.0"
		      << "&to=" << target_lang;

		if (source_lang != "auto") {
			route << "&from=" << source_lang;
		}

		// Create the request body
		json body = json::array({{{"Text", text}}});
		std::string requestBody = body.dump();

		// Construct full URL
		std::string url = endpoint_ + route.str();

		// Set up curl options
		curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlHelper::WriteCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);

		// Set up POST request
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, requestBody.c_str());

		// Set up headers
		struct curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");

		std::string auth_header = "Ocp-Apim-Subscription-Key: " + api_key_;
		headers = curl_slist_append(headers, auth_header.c_str());

		// Add location header if provided
		if (!location_.empty()) {
			std::string location_header = "Ocp-Apim-Subscription-Region: " + location_;
			headers = curl_slist_append(headers, location_header.c_str());
		}

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

	} catch (const json::exception &e) {
		throw TranslationError(std::string("JSON parsing error: ") + e.what());
	}
}

std::string AzureTranslator::parseResponse(const std::string &response_str)
{
	try {
		json response = json::parse(response_str);

		// Check for error response
		if (response.contains("error")) {
			const auto &error = response["error"];
			throw TranslationError("Azure API Error: " +
					       error.value("message", "Unknown error"));
		}

		// Azure returns an array of translations
		// Each translation can have multiple target languages
		// We'll take the first translation's first target
		return response[0]["translations"][0]["text"].get<std::string>();

	} catch (const json::exception &e) {
		throw TranslationError(std::string("Failed to parse Azure response: ") + e.what());
	}
}
