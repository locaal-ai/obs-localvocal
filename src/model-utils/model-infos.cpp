#include "model-downloader-types.h"
#include "plugin-support.h"

#include <obs-module.h>

#include <fstream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>


std::map<std::string, ModelInfo> models_info() {
	std::map<std::string, ModelInfo> models_info;
	// read the model infos from the json file and return it
	char *model_directory_json_file = obs_module_file("models/model_directory.json");
	if (model_directory_json_file == nullptr) {
		obs_log(LOG_ERROR, "Cannot find model directory file");
		return models_info;
	}
	obs_log(LOG_INFO, "model directory file: %s", model_directory_json_file);
	std::string model_directory_file_str = std::string(model_directory_json_file);
	bfree(model_directory_json_file);

	std::ifstream model_directory_file(model_directory_file_str);
	if (!model_directory_file.is_open()) {
		obs_log(LOG_ERROR, "Failed to open model directory file");
		return models_info;
	}

	// read the json file
	nlohmann::json model_directory_json;
	model_directory_file >> model_directory_json;

	// iterate over the json file and populate the models_info map
	for (const auto &model : model_directory_json) {
		ModelInfo model_info;
		model_info.friendly_name = model["friendly_name"];
		model_info.local_folder_name = model["local_folder_name"];
		model_info.type = model["type"];
		for (const auto &file : model["files"]) {
			ModelFileDownloadInfo file_info;
			file_info.url = file["url"];
			file_info.sha256 = file["sha256"];
			model_info.files.push_back(file_info);
		}
		models_info[model_info.local_folder_name] = model_info;
	}

	return models_info;
}
