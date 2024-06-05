#include <obs-module.h>

#include "translation-includes.h"
#include "translation.h"
#include "translation-utils.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"

void start_translation(struct transcription_filter_data *gf)
{
	obs_log(LOG_INFO, "Starting translation...");

	if (gf->translation_model_index == "!!!external!!!") {
		obs_log(LOG_INFO, "External model selected.");
		if (gf->translation_model_path_external.empty()) {
			obs_log(LOG_ERROR, "External model path is empty.");
			gf->translate = false;
			return;
		}
		std::string model_file_found = gf->translation_model_path_external;
		build_and_enable_translation(gf, model_file_found);
		return;
	}

	const ModelInfo &translation_model_info = models_info[gf->translation_model_index];
	std::string model_file_found = find_model_folder(translation_model_info);
	if (model_file_found == "") {
		obs_log(LOG_INFO, "Translation CT2 model does not exist. Downloading...");
		download_model_with_ui_dialog(
			translation_model_info,
			[gf, model_file_found](int download_status, const std::string &path) {
				if (download_status == 0) {
					obs_log(LOG_INFO, "CT2 model download complete");
					build_and_enable_translation(gf, path);
				} else {
					obs_log(LOG_ERROR, "Model download failed");
					gf->translate = false;
				}
			});
	} else {
		// Model exists, just load it
		build_and_enable_translation(gf, model_file_found);
	}
}
