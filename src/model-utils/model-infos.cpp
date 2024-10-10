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

/**
 * @brief Downloads a JSON file from a specified GitHub URL.
 *
 * This function uses libcurl to download a JSON file from a GitHub repository.
 * The downloaded content is stored in the provided string reference.
 *
 * @param json_content A reference to a string where the downloaded JSON content will be stored.
 * @return true if the download was successful and the HTTP response code was 200, false otherwise.
 *
 * The function performs the following steps:
 * - Initializes a CURL session.
 * - Sets the URL to download the JSON from.
 * - Sets the callback function to write the downloaded data.
 * - Follows redirects and sets a timeout of 10 seconds.
 * - Performs the download operation.
 * - Checks for errors and logs them using obs_log.
 * - Cleans up the CURL session.
 * - Checks the HTTP response code to ensure it is 200 (OK).
 */
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

/**
 * @brief Parses a JSON object to extract model information.
 *
 * This function takes a JSON object representing a model and extracts various
 * fields to populate a ModelInfo structure. It performs validation on the
 * presence and types of required fields and logs warnings for any missing or
 * invalid fields.
 *
 * @param model The JSON object containing the model information.
 * @return An optional ModelInfo object. If the required fields are missing or
 *         invalid, it returns std::nullopt.
 *
 * The JSON object is expected to have the following structure:
 * {
 *     "friendly_name": "string",          // Required
 *     "local_folder_name": "string",      // Optional
 *     "type": "string",                   // Optional, expected values: "MODEL_TYPE_TRANSCRIPTION" or "MODEL_TYPE_TRANSLATION"
 *     "files": [                          // Optional, array of file objects
 *         {
 *             "url": "string",            // Required in each file object
 *             "sha256": "string"          // Required in each file object
 *         },
 *         ...
 *     ],
 *     "extra": {                          // Optional
 *         "language": "string",           // Optional
 *         "description": "string",        // Optional
 *         "source": "string"              // Optional
 *     }
 * }
 */
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

			model_info.files.push_back(file_info);
		}
	} else {
		obs_log(LOG_WARNING, "Missing or invalid 'files' array for model: %s",
			model_info.friendly_name.c_str());
	}

	// Parse the new "extra" field
	if (model.contains("extra") && model["extra"].is_object()) {
		const auto &extra = model["extra"];
		if (extra.contains("language") && extra["language"].is_string())
			model_info.extra.language = extra["language"].get<std::string>();
		if (extra.contains("description") && extra["description"].is_string())
			model_info.extra.description = extra["description"].get<std::string>();
		if (extra.contains("source") && extra["source"].is_string())
			model_info.extra.source = extra["source"].get<std::string>();
	}

	return model_info;
}

/**
 * @brief Loads model information from a JSON source.
 *
 * This function attempts to download a JSON file containing model information from GitHub.
 * If the download fails, it falls back to loading the JSON file from a local directory.
 * The JSON file is expected to contain an array of models under the key "models".
 * Each model's information is parsed and stored in a map with the model's friendly name as the key.
 *
 * @return A map where the keys are model friendly names and the values are ModelInfo objects.
 */
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

const std::vector<ModelInfo> get_sorted_models_info()
{
	const auto &models_map = models_info();
	std::vector<ModelInfo> standard_models;
	std::vector<ModelInfo> huggingface_models;

	// Separate models into two categories
	for (const auto &[key, model] : models_map) {
		if (!model.extra.source.empty()) {
			huggingface_models.push_back(model);
		} else {
			standard_models.push_back(model);
		}
	}

	// Sort both vectors based on friendly_name
	auto sort_by_name = [](const ModelInfo &a, const ModelInfo &b) {
		return a.friendly_name < b.friendly_name;
	};
	std::sort(standard_models.begin(), standard_models.end(), sort_by_name);
	std::sort(huggingface_models.begin(), huggingface_models.end(), sort_by_name);

	// Combine the sorted vectors with a separator
	std::vector<ModelInfo> result = std::move(standard_models);
	if (!huggingface_models.empty()) {
		ModelInfo info;
		info.friendly_name = "--------- HuggingFace Models ---------";
		info.type = ModelType::MODEL_TYPE_TRANSCRIPTION;
		result.push_back(info);
		result.insert(result.end(), huggingface_models.begin(), huggingface_models.end());
	}

	return result;
}
