#include "model-downloader.h"
#include "plugin-support.h"
#include "model-downloader-ui.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <curl/curl.h>

std::string find_model_file(const std::string &model_name)
{
	const char *model_name_cstr = model_name.c_str();
	obs_log(LOG_INFO, "Checking if model %s exists in data...", model_name_cstr);

	char *model_file_path = obs_module_file(model_name_cstr);
	if (model_file_path == nullptr) {
		obs_log(LOG_INFO, "Model %s not found in data.", model_name_cstr);
	} else {
		std::string model_file_path_str(model_file_path);
		bfree(model_file_path);
		if (!std::filesystem::exists(model_file_path_str)) {
			obs_log(LOG_INFO, "Model not found in data: %s",
				model_file_path_str.c_str());
		} else {
			obs_log(LOG_INFO, "Model found in data: %s", model_file_path_str.c_str());
			return model_file_path_str;
		}
	}

	// Check if model exists in the config folder
	char *model_config_path_str =
		obs_module_get_config_path(obs_current_module(), model_name_cstr);
	std::string model_config_path(model_config_path_str);
	bfree(model_config_path_str);
	obs_log(LOG_INFO, "Model path in config: %s", model_config_path.c_str());
	if (std::filesystem::exists(model_config_path)) {
		obs_log(LOG_INFO, "Model exists in config folder: %s", model_config_path.c_str());
		return model_config_path;
	}

	obs_log(LOG_INFO, "Model %s not found.", model_name_cstr);
	return "";
}

void download_model_with_ui_dialog(const std::string &model_name,
				   download_finished_callback_t download_finished_callback)
{
	// Start the model downloader UI
	ModelDownloader *model_downloader = new ModelDownloader(
		model_name, download_finished_callback, (QWidget *)obs_frontend_get_main_window());
	model_downloader->show();
}
