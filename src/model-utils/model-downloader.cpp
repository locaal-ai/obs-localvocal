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

bool check_if_model_exists(const std::string &model_name)
{
	obs_log(LOG_INFO, "Checking if model %s exists...", model_name.c_str());
	char *model_file_path = obs_module_file(model_name.c_str());
	obs_log(LOG_INFO, "Model file path: %s", model_file_path);
	if (model_file_path == nullptr) {
		obs_log(LOG_INFO, "Model %s does not exist.", model_name.c_str());
		return false;
	}

	if (!std::filesystem::exists(model_file_path)) {
		obs_log(LOG_INFO, "Model %s does not exist.", model_file_path);
		bfree(model_file_path);
		return false;
	}
	bfree(model_file_path);
	return true;
}

void download_model_with_ui_dialog(
	const std::string &model_name,
	std::function<void(int download_status)> download_finished_callback)
{
	// Start the model downloader UI
	ModelDownloader *model_downloader = new ModelDownloader(
		model_name, download_finished_callback, (QWidget *)obs_frontend_get_main_window());
	model_downloader->show();
}
