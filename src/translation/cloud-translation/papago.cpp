#include "papago.h"
#include "curl-helper.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

// Language pair support mapping
struct LanguagePairHash {
	size_t operator()(const std::pair<std::string, std::string> &p) const
	{
		return std::hash<std::string>()(p.first + p.second);
	}
};

PapagoTranslator::PapagoTranslator(const std::string &client_id, const std::string &client_secret)
	: client_id_(client_id),
	  client_secret_(client_secret),
	  curl_helper_(std::make_unique<CurlHelper>())
{
}

PapagoTranslator::~PapagoTranslator() = default;

std::string PapagoTranslator::mapLanguageCode(const std::string &lang_code) const
{
	// Map common ISO language codes to Papago codes
	static const std::unordered_map<std::string, std::string> code_map = {
		{"auto", "auto"},   {"ko", "ko"}, // Korean
		{"en", "en"},                     // English
		{"ja", "ja"},                     // Japanese
		{"zh", "zh-CN"},                  // Chinese (Simplified)
		{"zh-CN", "zh-CN"},               // Chinese (Simplified)
		{"zh-TW", "zh-TW"},               // Chinese (Traditional)
		{"vi", "vi"},                     // Vietnamese
		{"th", "th"},                     // Thai
		{"id", "id"},                     // Indonesian
		{"fr", "fr"},                     // French
		{"es", "es"},                     // Spanish
		{"ru", "ru"},                     // Russian
		{"de", "de"},                     // German
		{"it", "it"}                      // Italian
	};

	auto it = code_map.find(lang_code);
	if (it != code_map.end()) {
		return it->second;
	}
	throw TranslationError("Unsupported language code: " + lang_code);
}

bool PapagoTranslator::isLanguagePairSupported(const std::string &source,
					       const std::string &target) const
{
	static const std::unordered_set<std::pair<std::string, std::string>, LanguagePairHash>
		supported_pairs = {// Korean pairs
				   {"ko", "en"},
				   {"en", "ko"},
				   {"ko", "ja"},
				   {"ja", "ko"},
				   {"ko", "zh-CN"},
				   {"zh-CN", "ko"},
				   {"ko", "zh-TW"},
				   {"zh-TW", "ko"},
				   {"ko", "vi"},
				   {"vi", "ko"},
				   {"ko", "th"},
				   {"th", "ko"},
				   {"ko", "id"},
				   {"id", "ko"},
				   {"ko", "fr"},
				   {"fr", "ko"},
				   {"ko", "es"},
				   {"es", "ko"},
				   {"ko", "ru"},
				   {"ru", "ko"},
				   {"ko", "de"},
				   {"de", "ko"},
				   {"ko", "it"},
				   {"it", "ko"},

				   // English pairs
				   {"en", "ja"},
				   {"ja", "en"},
				   {"en", "zh-CN"},
				   {"zh-CN", "en"},
				   {"en", "zh-TW"},
				   {"zh-TW", "en"},
				   {"en", "vi"},
				   {"vi", "en"},
				   {"en", "th"},
				   {"th", "en"},
				   {"en", "id"},
				   {"id", "en"},
				   {"en", "fr"},
				   {"fr", "en"},
				   {"en", "es"},
				   {"es", "en"},
				   {"en", "ru"},
				   {"ru", "en"},
				   {"en", "de"},
				   {"de", "en"},

				   // Japanese pairs
				   {"ja", "zh-CN"},
				   {"zh-CN", "ja"},
				   {"ja", "zh-TW"},
				   {"zh-TW", "ja"},
				   {"ja", "vi"},
				   {"vi", "ja"},
				   {"ja", "th"},
				   {"th", "ja"},
				   {"ja", "id"},
				   {"id", "ja"},
				   {"ja", "fr"},
				   {"fr", "ja"},

				   // Chinese pairs
				   {"zh-CN", "zh-TW"},
				   {"zh-TW", "zh-CN"}};

	// Special case for auto detection
	if (source == "auto") {
		return true;
	}

	return supported_pairs.find({source, target}) != supported_pairs.end();
}

std::string PapagoTranslator::translate(const std::string &text, const std::string &target_lang,
					const std::string &source_lang)
{
	if (text.length() > 5000) {
		throw TranslationError("Text exceeds maximum length of 5000 characters");
	}

	std::string target_lang_valid = target_lang;
	target_lang_valid.erase(std::remove(target_lang_valid.begin(), target_lang_valid.end(),
					    '_'),
				target_lang_valid.end());

	std::string papago_source = mapLanguageCode(source_lang);
	std::string papago_target = mapLanguageCode(target_lang_valid);

	if (!isLanguagePairSupported(papago_source, papago_target)) {
		throw TranslationError("Unsupported language pair: " + source_lang + " to " +
				       target_lang);
	}

	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
								 curl_easy_cleanup);

	if (!curl) {
		throw TranslationError("Failed to initialize CURL session");
	}

	std::string response;

	try {
		// Prepare request data
		std::string url = "https://naveropenapi.apigw.ntruss.com/nmt/v1/translation";

		// Create request body
		json request_body = {{"source", papago_source},
				     {"target", papago_target},
				     {"text", text}};
		std::string payload = request_body.dump();

		// Set up headers
		struct curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers,
					    ("X-NCP-APIGW-API-KEY-ID: " + client_id_).c_str());
		headers = curl_slist_append(headers,
					    ("X-NCP-APIGW-API-KEY: " + client_secret_).c_str());

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
			throw TranslationError("HTTP error: " + std::to_string(response_code));
		}

		return parseResponse(response);

	} catch (const json::exception &e) {
		throw TranslationError(std::string("JSON parsing error: ") + e.what());
	}
}

std::string PapagoTranslator::parseResponse(const std::string &response_str)
{
	try {
		json response = json::parse(response_str);

		if (!response.contains("message")) {
			throw TranslationError("Invalid response format from Papago API");
		}

		const auto &message = response["message"];
		if (!message.contains("result") || !message["result"].contains("translatedText")) {
			throw TranslationError("Translation result not found in response");
		}

		return message["result"]["translatedText"].get<std::string>();

	} catch (const json::exception &e) {
		throw TranslationError(std::string("Failed to parse Papago response: ") + e.what());
	}
}
