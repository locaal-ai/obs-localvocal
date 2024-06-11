#ifdef _WIN32
#define NOMINMAX
#endif

#include <obs-module.h>

#include "whisper-utils.h"
#include "whisper-processing.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"

void update_whisper_model(struct transcription_filter_data *gf, obs_data_t *s)
{
	// update the whisper model path
	std::string new_model_path = obs_data_get_string(s, "whisper_model_path");
	const bool is_external_model = new_model_path.find("!!!external!!!") != std::string::npos;

	char *silero_vad_model_file = obs_module_file("models/silero-vad/silero_vad.onnx");
	if (silero_vad_model_file == nullptr) {
		obs_log(LOG_ERROR, "Cannot find Silero VAD model file");
		return;
	}
	obs_log(gf->log_level, "Silero VAD model file: %s", silero_vad_model_file);
	std::string silero_vad_model_file_str = std::string(silero_vad_model_file);
	bfree(silero_vad_model_file);

	if (gf->whisper_model_path.empty() || gf->whisper_model_path != new_model_path ||
	    is_external_model) {

		if (gf->whisper_model_path != new_model_path) {
			// model path changed
			obs_log(gf->log_level, "model path changed from %s to %s",
				gf->whisper_model_path.c_str(), new_model_path.c_str());
		}

		// check if the new model is external file
		if (!is_external_model) {
			// new model is not external file
			shutdown_whisper_thread(gf);

			if (models_info.count(new_model_path) == 0) {
				obs_log(LOG_WARNING, "Model '%s' does not exist",
					new_model_path.c_str());
				return;
			}

			const ModelInfo &model_info = models_info[new_model_path];

			// check if the model exists, if not, download it
			std::string model_file_found = find_model_bin_file(model_info);
			if (model_file_found == "") {
				obs_log(LOG_WARNING, "Whisper model does not exist");
				download_model_with_ui_dialog(
					model_info,
					[gf, new_model_path, silero_vad_model_file_str](
						int download_status, const std::string &path) {
						if (download_status == 0) {
							obs_log(LOG_INFO,
								"Model download complete");
							gf->whisper_model_path = new_model_path;
							start_whisper_thread_with_path(
								gf, path,
								silero_vad_model_file_str.c_str());
						} else {
							obs_log(LOG_ERROR, "Model download failed");
						}
					});
			} else {
				// Model exists, just load it
				gf->whisper_model_path = new_model_path;
				start_whisper_thread_with_path(gf, model_file_found,
							       silero_vad_model_file_str.c_str());
			}
		} else {
			// new model is external file, get file location from file property
			std::string external_model_file_path =
				obs_data_get_string(s, "whisper_model_path_external");
			if (external_model_file_path.empty()) {
				obs_log(LOG_WARNING, "External model file path is empty");
			} else {
				// check if the external model file is not currently loaded
				if (gf->whisper_model_file_currently_loaded ==
				    external_model_file_path) {
					obs_log(LOG_INFO, "External model file is already loaded");
					return;
				} else {
					shutdown_whisper_thread(gf);
					gf->whisper_model_path = new_model_path;
					start_whisper_thread_with_path(
						gf, external_model_file_path,
						silero_vad_model_file_str.c_str());
				}
			}
		}
	} else {
		// model path did not change
		obs_log(gf->log_level, "Model path did not change: %s == %s",
			gf->whisper_model_path.c_str(), new_model_path.c_str());
	}

	const bool new_dtw_timestamps = obs_data_get_bool(s, "dtw_token_timestamps");

	if (new_dtw_timestamps != gf->enable_token_ts_dtw) {
		// dtw_token_timestamps changed
		obs_log(gf->log_level, "dtw_token_timestamps changed from %d to %d",
			gf->enable_token_ts_dtw, new_dtw_timestamps);
		gf->enable_token_ts_dtw = obs_data_get_bool(s, "dtw_token_timestamps");
		shutdown_whisper_thread(gf);
		start_whisper_thread_with_path(gf, gf->whisper_model_path,
					       silero_vad_model_file_str.c_str());
	}
}
