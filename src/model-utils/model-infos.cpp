#include "model-downloader-types.h"
#include "plugin-support.h"

#include <obs-module.h>

#include <fstream>
#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <optional>

#include <nlohmann/json.hpp>
#include <curl/curl.h>

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	((std::string *)userp)->append((char *)contents, size * nmemb);
	return size * nmemb;
}

bool download_json_from_github(std::string &json_content)
{
	CURL *curl;
	CURLcode res;
	std::string readBuffer;
	long http_code = 0;

	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(
			curl, CURLOPT_URL,
			"https://raw.githubusercontent.com/locaal-ai/obs-localvocal/master/data/models/models_directory.json");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);       // Set a timeout (10 seconds)

		res = curl_easy_perform(curl);

		if (res != CURLE_OK) {
			obs_log(LOG_ERROR, "Failed to download JSON from GitHub: %s",
				curl_easy_strerror(res));
			curl_easy_cleanup(curl);
			return false;
		}

		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		curl_easy_cleanup(curl);

		if (http_code != 200) {
			obs_log(LOG_ERROR, "HTTP error: %ld", http_code);
			return false;
		}
	} else {
		obs_log(LOG_ERROR, "Failed to initialize curl");
		return false;
	}

	json_content = readBuffer;
	return true;
}

std::optional<ModelInfo> parse_model_json(const nlohmann::json &model)
{
	ModelInfo model_info;

	if (!model.contains("friendly_name") || !model["friendly_name"].is_string()) {
		obs_log(LOG_WARNING,
			"Missing or invalid 'friendly_name' for a model. Skipping this model.");
		return std::nullopt;
	}
	model_info.friendly_name = model["friendly_name"].get<std::string>();

	if (model.contains("local_folder_name") && model["local_folder_name"].is_string()) {
		model_info.local_folder_name = model["local_folder_name"].get<std::string>();
	} else {
		obs_log(LOG_WARNING, "Missing or invalid 'local_folder_name' for model: %s",
			model_info.friendly_name.c_str());
	}

	if (model.contains("type") && model["type"].is_string()) {
		const std::string &type_str = model["type"].get<std::string>();
		if (type_str == "MODEL_TYPE_TRANSCRIPTION")
			model_info.type = ModelType::MODEL_TYPE_TRANSCRIPTION;
		else if (type_str == "MODEL_TYPE_TRANSLATION")
			model_info.type = ModelType::MODEL_TYPE_TRANSLATION;
		else
			obs_log(LOG_WARNING, "Invalid 'type' for model: %s",
				model_info.friendly_name.c_str());
	} else {
		obs_log(LOG_WARNING, "Missing or invalid 'type' for model: %s",
			model_info.friendly_name.c_str());
	}

	if (model.contains("files") && model["files"].is_array()) {
		for (const auto &file : model["files"]) {
			ModelFileDownloadInfo file_info;

			if (file.contains("url") && file["url"].is_string())
				file_info.url = file["url"].get<std::string>();
			else
				obs_log(LOG_WARNING,
					"Missing or invalid 'url' for a file in model: %s",
					model_info.friendly_name.c_str());

			if (file.contains("sha256") && file["sha256"].is_string())
				file_info.sha256 = file["sha256"].get<std::string>();
			else
				obs_log(LOG_WARNING,
					"Missing or invalid 'sha256' for a file in model: %s",
					model_info.friendly_name.c_str());

			model_info.files.push_back(file_info);
		}
	} else {
		obs_log(LOG_WARNING, "Missing or invalid 'files' array for model: %s",
			model_info.friendly_name.c_str());
	}

	return model_info;
}

std::map<std::string, ModelInfo> load_models_info()
{
	std::map<std::string, ModelInfo> models_info_map;
	nlohmann::json model_directory_json;

	// Try to download from GitHub first
	std::string github_json_content;
	bool download_success = download_json_from_github(github_json_content);

	if (download_success) {
		obs_log(LOG_INFO, "Successfully downloaded models directory from GitHub");
		std::istringstream json_stream(github_json_content);
		json_stream >> model_directory_json;
	} else {
		// Fall back to local file
		obs_log(LOG_INFO, "Falling back to local models directory file");
		char *model_directory_json_file = obs_module_file("models/models_directory.json");
		if (model_directory_json_file == nullptr) {
			obs_log(LOG_ERROR, "Cannot find local model directory file");
			return models_info_map;
		}
		obs_log(LOG_INFO, "Local model directory file: %s", model_directory_json_file);
		std::string model_directory_file_str = std::string(model_directory_json_file);
		bfree(model_directory_json_file);

		std::ifstream model_directory_file(model_directory_file_str);
		if (!model_directory_file.is_open()) {
			obs_log(LOG_ERROR, "Failed to open local model directory file");
			return models_info_map;
		}

		model_directory_file >> model_directory_json;
	}

	if (!model_directory_json.contains("models") ||
	    !model_directory_json["models"].is_array()) {
		obs_log(LOG_ERROR, "Invalid JSON structure: 'models' array not found");
		return models_info_map;
	}

	for (const auto &model : model_directory_json["models"]) {
		auto model_info_opt = parse_model_json(model);
		if (model_info_opt) {
			models_info_map[model_info_opt->friendly_name] = *model_info_opt;
		}
	}

	obs_log(LOG_INFO, "Loaded %zu models", models_info_map.size());

	return models_info_map;
}

const std::map<std::string, ModelInfo> &models_info()
{
	static const std::unique_ptr<const std::map<std::string, ModelInfo>> cached_models_info =
		std::make_unique<const std::map<std::string, ModelInfo>>(load_models_info());

	return *cached_models_info;
}
