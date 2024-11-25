#include <iostream>
#include <memory>
#include <string>
#include <stdexcept>

#include "ITranslator.h"
#include "google-cloud.h"
#include "deepl.h"
#include "azure.h"
#include "papago.h"
#include "claude.h"
#include "openai.h"
#include "custom-api.h"

#include "plugin-support.h"
#include <util/base.h>

#include "translation-cloud.h"

std::unique_ptr<ITranslator> createTranslator(const CloudTranslatorConfig &config)
{
	if (config.provider == "google") {
		return std::make_unique<GoogleTranslator>(config.access_key);
	} else if (config.provider == "deepl") {
		return std::make_unique<DeepLTranslator>(config.access_key, config.free);
	} else if (config.provider == "azure") {
		return std::make_unique<AzureTranslator>(config.access_key, config.region);
		// } else if (config.provider == "aws") {
		//     return std::make_unique<AWSTranslator>(config.access_key, config.secret_key, config.region);
	} else if (config.provider == "papago") {
		return std::make_unique<PapagoTranslator>(config.access_key, config.secret_key);
	} else if (config.provider == "claude") {
		return std::make_unique<ClaudeTranslator>(
			config.access_key,
			config.model.empty() ? "claude-3-sonnet-20240229" : config.model);
	} else if (config.provider == "openai") {
		return std::make_unique<OpenAITranslator>(
			config.access_key,
			config.model.empty() ? "gpt-4-turbo-preview" : config.model);
	} else if (config.provider == "api") {
		return std::make_unique<CustomApiTranslator>(config.endpoint, config.body,
							     config.response_json_path);
	}
	throw TranslationError("Unknown translation provider: " + config.provider);
}

std::string translate_cloud(const CloudTranslatorConfig &config, const std::string &text,
			    const std::string &target_lang, const std::string &source_lang)
{
	try {
		auto translator = createTranslator(config);
		obs_log(LOG_INFO, "translate with cloud provider %s. %s -> %s",
			config.provider.c_str(), source_lang.c_str(), target_lang.c_str());
		std::string result = translator->translate(text, target_lang, source_lang);
		return result;
	} catch (const TranslationError &e) {
		obs_log(LOG_ERROR, "Translation error: %s\n", e.what());
	}
	return "";
}
