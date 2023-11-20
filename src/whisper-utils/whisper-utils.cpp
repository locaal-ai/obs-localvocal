#include "whisper-utils.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"
#include "whisper-processing.h"


void update_whsiper_model_path(struct transcription_filter_data *gf, obs_data_t *s) {
    // update the whisper model path
	std::string new_model_path = obs_data_get_string(s, "whisper_model_path");
	const bool is_external_model = new_model_path.find("!!!external!!!") != std::string::npos;

	if (gf->whisper_model_path == nullptr ||
	    strcmp(new_model_path.c_str(), gf->whisper_model_path) != 0 || is_external_model) {
		// model path changed, reload the model
		obs_log(gf->log_level, "model path changed from %s to %s", gf->whisper_model_path,
			new_model_path.c_str());

		// check if the new model is external file
		if (!is_external_model) {
			// new model is not external file
			shutdown_whisper_thread(gf);

			gf->whisper_model_path = bstrdup(new_model_path.c_str());

			// check if the model exists, if not, download it
			std::string model_file_found = find_model_file(gf->whisper_model_path);
			if (model_file_found == "") {
				obs_log(LOG_WARNING, "Whisper model does not exist");
				download_model_with_ui_dialog(
					gf->whisper_model_path,
					[gf](int download_status, const std::string &path) {
						if (download_status == 0) {
							obs_log(LOG_INFO,
								"Model download complete");
							start_whisper_thread_with_path(gf, path);
						} else {
							obs_log(LOG_ERROR, "Model download failed");
						}
					});
			} else {
				// Model exists, just load it
				start_whisper_thread_with_path(gf, model_file_found);
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
					gf->whisper_model_path = bstrdup(new_model_path.c_str());
					start_whisper_thread_with_path(gf,
								       external_model_file_path);
				}
			}
		}
	} else {
		// model path did not change
		obs_log(LOG_DEBUG, "model path did not change: %s == %s", gf->whisper_model_path,
			new_model_path.c_str());
	}
}

void shutdown_whisper_thread(struct transcription_filter_data *gf)
{
	obs_log(gf->log_level, "shutdown_whisper_thread");
	if (gf->whisper_context != nullptr) {
		// acquire the mutex before freeing the context
		if (!gf->whisper_ctx_mutex || !gf->wshiper_thread_cv) {
			obs_log(LOG_ERROR, "whisper_ctx_mutex is null");
			return;
		}
		std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);
		whisper_free(gf->whisper_context);
		gf->whisper_context = nullptr;
		gf->wshiper_thread_cv->notify_all();
	}
	if (gf->whisper_thread.joinable()) {
		gf->whisper_thread.join();
	}
	if (gf->whisper_model_path != nullptr) {
		bfree(gf->whisper_model_path);
		gf->whisper_model_path = nullptr;
	}
}

void start_whisper_thread_with_path(struct transcription_filter_data *gf, const std::string &path)
{
	obs_log(gf->log_level, "start_whisper_thread_with_path: %s", path.c_str());
	if (!gf->whisper_ctx_mutex) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_ctx_mutex is null");
		return;
	}
	std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);
	if (gf->whisper_context != nullptr) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_context is not null");
		return;
	}
	gf->whisper_context = init_whisper_context(path);
	gf->whisper_model_file_currently_loaded = path;
	std::thread new_whisper_thread(whisper_loop, gf);
	gf->whisper_thread.swap(new_whisper_thread);
}
