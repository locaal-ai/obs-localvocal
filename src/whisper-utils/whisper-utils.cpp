#include "whisper-utils.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"
#include "whisper-processing.h"

#include <obs-module.h>

void update_whsiper_model(struct transcription_filter_data *gf, obs_data_t *s)
{
	// update the whisper model path
	std::string new_model_path = obs_data_get_string(s, "whisper_model_path");
	const bool is_external_model = new_model_path.find("!!!external!!!") != std::string::npos;

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
					model_info, [gf, new_model_path](int download_status,
									 const std::string &path) {
						if (download_status == 0) {
							obs_log(LOG_INFO,
								"Model download complete");
							gf->whisper_model_path = new_model_path;
							start_whisper_thread_with_path(gf, path);
						} else {
							obs_log(LOG_ERROR, "Model download failed");
						}
					});
			} else {
				// Model exists, just load it
				gf->whisper_model_path = new_model_path;
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
					gf->whisper_model_path = new_model_path;
					start_whisper_thread_with_path(gf,
								       external_model_file_path);
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
		start_whisper_thread_with_path(gf, gf->whisper_model_path);
	} else {
		// dtw_token_timestamps did not change
		obs_log(gf->log_level, "dtw_token_timestamps did not change: %d == %d",
			gf->enable_token_ts_dtw, new_dtw_timestamps);
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
	if (!gf->whisper_model_path.empty()) {
		gf->whisper_model_path = "";
	}
}

void start_whisper_thread_with_path(struct transcription_filter_data *gf, const std::string &path)
{
	obs_log(gf->log_level, "start_whisper_thread_with_path: %s", path.c_str());
	if (gf->whisper_ctx_mutex == nullptr) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_ctx_mutex is null");
		return;
	}
	std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);
	if (gf->whisper_context != nullptr) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_context is not null");
		return;
	}

	// initialize Silero VAD
	char *silero_vad_model_file = obs_module_file("models/silero-vad/silero_vad.onnx");
#ifdef _WIN32
	std::wstring silero_vad_model_path;
	silero_vad_model_path.assign(silero_vad_model_file,
				     silero_vad_model_file + strlen(silero_vad_model_file));
#else
	std::string silero_vad_model_path = silero_vad_model_file;
#endif
    // roughly following https://github.com/SYSTRAN/faster-whisper/blob/master/faster_whisper/vad.py
    // for silero vad parameters
	gf->vad.reset(new VadIterator(silero_vad_model_path, WHISPER_SAMPLE_RATE, 64, 0.5f, 1000, 200, 250));

	gf->whisper_context = init_whisper_context(path, gf);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_ERROR, "Failed to initialize whisper context");
		return;
	}
	gf->whisper_model_file_currently_loaded = path;
	std::thread new_whisper_thread(whisper_loop, gf);
	gf->whisper_thread.swap(new_whisper_thread);
}

// Finds start of 2-token overlap between two sequences of tokens
// Returns a pair of indices of the first overlapping tokens in the two sequences
// If no overlap is found, the function returns {-1, -1}
// Allows for a single token mismatch in the overlap
std::pair<int, int> findStartOfOverlap(const std::vector<whisper_token_data> &seq1,
                                       const std::vector<whisper_token_data> &seq2)
{
    if (seq1.empty() || seq2.empty() || seq1.size() == 1 || seq2.size() == 1) {
        return {-1, -1};
    }
    for (int i = 0; i < seq1.size() - 1; ++i) {
        for (int j = 0; j < seq2.size() - 1; ++j) {
            if (seq1[i].id == seq2[j].id) {
                // Check if the next token in both sequences is the same
                if (seq1[i + 1].id == seq2[j + 1].id) {
                    return {i, j};
                }
                // 1-skip check on seq1
                if (i + 2 < seq1.size() && seq1[i + 2].id == seq2[j + 1].id) {
                    return {i, j};
                }
                // 1-skip check on seq2
                if (j + 2 < seq2.size() && seq1[i + 1].id == seq2[j + 2].id) {
                    return {i, j};
                }
            }
        }
    }
    return {-1, -1};
}

// Function to reconstruct a whole sentence from two sentences using overlap info
// If no overlap is found, the function returns the concatenation of the two sequences
std::vector<whisper_token_data> reconstructSentence(const std::vector<whisper_token_data> &seq1,
						    const std::vector<whisper_token_data> &seq2)
{
	auto overlap = findStartOfOverlap(seq1, seq2);
	std::vector<whisper_token_data> reconstructed;

	if (overlap.first == -1 || overlap.second == -1) {
		// Return concat of seq1 and seq2 if no overlap found
        
        // check if the last token of seq1 == the first token of seq2
        if (!seq1.empty() && !seq2.empty() && seq1.back().id == seq2.front().id) {
            // don't add the last token of seq1
            reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end() - 1);
        } else {
            // add all tokens of seq1
            reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end());
        }
        reconstructed.insert(reconstructed.end(), seq2.begin(), seq2.end());
        return reconstructed;
	}

	// Add tokens from the first sequence up to the overlap
	reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.begin() + overlap.first);

	// Determine the length of the overlap
	int overlapLength = 0;
	while (overlap.first + overlapLength < seq1.size() &&
	       overlap.second + overlapLength < seq2.size() &&
	       seq1[overlap.first + overlapLength].id == seq2[overlap.second + overlapLength].id) {
		overlapLength++;
	}

	// Add overlapping tokens
	reconstructed.insert(reconstructed.end(), seq1.begin() + overlap.first,
			     seq1.begin() + overlap.first + overlapLength);

	// Add remaining tokens from the second sequence
	reconstructed.insert(reconstructed.end(), seq2.begin() + overlap.second + overlapLength,
			     seq2.end());

	return reconstructed;
}
