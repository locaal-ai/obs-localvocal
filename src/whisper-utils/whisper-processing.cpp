#include <whisper.h>

#include <obs-module.h>

#include <util/profiler.hpp>

#include "plugin-support.h"
#include "transcription-filter-data.h"
#include "whisper-processing.h"
#include "whisper-utils.h"
#include "transcription-utils.h"

#ifdef _WIN32
#include <fstream>
#define NOMINMAX
#include <Windows.h>
#endif

#include "model-utils/model-find-utils.h"
#include "vad-processing.h"

#include <algorithm>
#include <chrono>
#include <regex>

struct whisper_context *init_whisper_context(const std::string &model_path_in,
					     struct transcription_filter_data *gf)
{
	std::string model_path = model_path_in;

	obs_log(LOG_INFO, "Loading whisper model from %s", model_path.c_str());

	if (std::filesystem::is_directory(model_path)) {
		obs_log(LOG_INFO,
			"Model path is a directory, not a file, looking for .bin file in folder");
		// look for .bin file
		const std::string model_bin_file = find_bin_file_in_folder(model_path);
		if (model_bin_file.empty()) {
			obs_log(LOG_ERROR, "Model bin file not found in folder: %s",
				model_path.c_str());
			return nullptr;
		}
		model_path = model_bin_file;
	}

	whisper_log_set(
		[](enum ggml_log_level level, const char *text, void *user_data) {
			UNUSED_PARAMETER(level);
			struct transcription_filter_data *ctx =
				static_cast<struct transcription_filter_data *>(user_data);
			// remove trailing newline
			char *text_copy = bstrdup(text);
			text_copy[strcspn(text_copy, "\n")] = 0;
			obs_log(ctx->log_level, "Whisper: %s", text_copy);
			bfree(text_copy);
		},
		gf);

	struct whisper_context_params cparams = whisper_context_default_params();
#ifdef LOCALVOCAL_WITH_CUDA
	cparams.use_gpu = true;
	obs_log(LOG_INFO, "Using CUDA GPU for inference, device %d", cparams.gpu_device);
#elif defined(LOCALVOCAL_WITH_HIPBLAS)
	cparams.use_gpu = true;
	obs_log(LOG_INFO, "Using hipBLAS for inference");
#elif defined(__APPLE__)
	cparams.use_gpu = true;
	obs_log(LOG_INFO, "Using Metal/CoreML for inference");
#else
	cparams.use_gpu = false;
	obs_log(LOG_INFO, "Using CPU for inference");
#endif

	cparams.dtw_token_timestamps = gf->enable_token_ts_dtw;
	if (gf->enable_token_ts_dtw) {
		obs_log(LOG_INFO, "DTW token timestamps enabled");
		cparams.dtw_aheads_preset = WHISPER_AHEADS_TINY_EN;
		// cparams.dtw_n_top = 4;
	} else {
		obs_log(LOG_INFO, "DTW token timestamps disabled");
		cparams.dtw_aheads_preset = WHISPER_AHEADS_NONE;
	}

	struct whisper_context *ctx = nullptr;
	try {
#ifdef _WIN32
		// convert model path UTF8 to wstring (wchar_t) for whisper
		int count = MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(),
						(int)model_path.length(), NULL, 0);
		std::wstring model_path_ws(count, 0);
		MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), (int)model_path.length(),
				    &model_path_ws[0], count);

		// Read model into buffer
		std::ifstream modelFile(model_path_ws, std::ios::binary);
		if (!modelFile.is_open()) {
			obs_log(LOG_ERROR, "Failed to open whisper model file %s",
				model_path.c_str());
			return nullptr;
		}
		modelFile.seekg(0, std::ios::end);
		const size_t modelFileSize = modelFile.tellg();
		modelFile.seekg(0, std::ios::beg);
		std::vector<char> modelBuffer(modelFileSize);
		modelFile.read(modelBuffer.data(), modelFileSize);
		modelFile.close();

		// Initialize whisper
		ctx = whisper_init_from_buffer_with_params(modelBuffer.data(), modelFileSize,
							   cparams);
#else
		ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
#endif
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Exception while loading whisper model: %s", e.what());
		return nullptr;
	}
	if (ctx == nullptr) {
		obs_log(LOG_ERROR, "Failed to load whisper model");
		return nullptr;
	}

	obs_log(LOG_INFO, "Whisper model loaded: %s", whisper_print_system_info());
	return ctx;
}

struct DetectionResultWithText run_whisper_inference(struct transcription_filter_data *gf,
						     const float *pcm32f_data_,
						     size_t pcm32f_num_samples, uint64_t t0 = 0,
						     uint64_t t1 = 0,
						     int vad_state = VAD_STATE_WAS_OFF)
{
	if (gf == nullptr) {
		obs_log(LOG_ERROR, "run_whisper_inference: gf is null");
		return {DETECTION_RESULT_UNKNOWN, "", t0, t1, {}, ""};
	}

	if (pcm32f_data_ == nullptr || pcm32f_num_samples == 0) {
		obs_log(LOG_ERROR, "run_whisper_inference: pcm32f_data is null or size is 0");
		return {DETECTION_RESULT_UNKNOWN, "", t0, t1, {}, ""};
	}

	// if the time difference between t0 and t1 is less than 50 ms - skip
	if (t1 - t0 < 50) {
		obs_log(gf->log_level,
			"Time difference between t0 and t1 is less than 50 ms, skipping");
		return {DETECTION_RESULT_UNKNOWN, "", t0, t1, {}, ""};
	}

	obs_log(gf->log_level, "%s: processing %d samples, %.3f sec, %d threads", __func__,
		int(pcm32f_num_samples), float(pcm32f_num_samples) / WHISPER_SAMPLE_RATE,
		gf->whisper_params.n_threads);

	bool should_free_buffer = false;
	float *pcm32f_data = (float *)pcm32f_data_;
	size_t pcm32f_size = pcm32f_num_samples;

	// incoming duration in ms
	const uint64_t incoming_duration_ms =
		(uint64_t)(pcm32f_num_samples * 1000 / WHISPER_SAMPLE_RATE);

	if (pcm32f_num_samples < WHISPER_SAMPLE_RATE) {
		obs_log(gf->log_level,
			"Speech segment is less than 1 second, padding with white noise to 1 second");
		const size_t new_size = (size_t)(1.01f * (float)(WHISPER_SAMPLE_RATE));
		// create a new buffer and copy the data to it in the middle
		pcm32f_data = (float *)bzalloc(new_size * sizeof(float));

		// add low volume white noise
		const float noise_level = 0.01f;
		for (size_t i = 0; i < new_size; ++i) {
			pcm32f_data[i] =
				noise_level * ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f);
		}

		memcpy(pcm32f_data + (new_size - pcm32f_num_samples) / 2, pcm32f_data_,
		       pcm32f_num_samples * sizeof(float));
		pcm32f_size = new_size;
		should_free_buffer = true;
	}

	// duration in ms
	const uint64_t whisper_duration_ms = (uint64_t)(pcm32f_size * 1000 / WHISPER_SAMPLE_RATE);

	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_WARNING, "whisper context is null");
		return {DETECTION_RESULT_UNKNOWN, "", t0, t1, {}, ""};
	}

	if (gf->n_context_sentences > 0 && !gf->last_transcription_sentence.empty()) {
		// set the initial prompt to the last transcription sentences (concatenated)
		std::string initial_prompt = gf->last_transcription_sentence[0];
		for (size_t i = 1; i < gf->last_transcription_sentence.size(); ++i) {
			initial_prompt += " " + gf->last_transcription_sentence[i];
		}
		gf->whisper_params.initial_prompt = initial_prompt.c_str();
		obs_log(gf->log_level, "Initial prompt: %s", gf->whisper_params.initial_prompt);
	}

	// run the inference
	int whisper_full_result = -1;
	gf->whisper_params.duration_ms = (int)(whisper_duration_ms);
	try {
		whisper_full_result = whisper_full(gf->whisper_context, gf->whisper_params,
						   pcm32f_data, (int)pcm32f_size);
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Whisper exception: %s. Filter restart is required", e.what());
		whisper_free(gf->whisper_context);
		gf->whisper_context = nullptr;
		if (should_free_buffer) {
			bfree(pcm32f_data);
		}
		return {DETECTION_RESULT_UNKNOWN, "", t0, t1, {}, ""};
	}
	if (should_free_buffer) {
		bfree(pcm32f_data);
	}

	std::string language = gf->whisper_params.language;
	if (gf->whisper_params.language == nullptr || strlen(gf->whisper_params.language) == 0 ||
	    strcmp(gf->whisper_params.language, "auto") == 0) {
		int lang_id = whisper_lang_auto_detect(gf->whisper_context, 0, 1, nullptr);
		language = whisper_lang_str(lang_id);
		obs_log(gf->log_level, "Detected language: %s", language.c_str());
	}

	if (whisper_full_result != 0) {
		obs_log(LOG_WARNING, "failed to process audio, error %d", whisper_full_result);
		return {DETECTION_RESULT_UNKNOWN, "", t0, t1, {}, ""};
	}

	float sentence_p = 0.0f;
	std::string text = "";
	std::string tokenIds = "";
	std::vector<whisper_token_data> tokens;
	for (int n_segment = 0; n_segment < whisper_full_n_segments(gf->whisper_context);
	     ++n_segment) {
		const int n_tokens = whisper_full_n_tokens(gf->whisper_context, n_segment);
		for (int j = 0; j < n_tokens; ++j) {
			// get token
			whisper_token_data token =
				whisper_full_get_token_data(gf->whisper_context, n_segment, j);
			const std::string token_str =
				whisper_token_to_str(gf->whisper_context, token.id);
			bool keep = true;
			// if the token starts with '[' and ends with ']', don't keep it
			if (token_str[0] == '[' && token_str[token_str.size() - 1] == ']') {
				keep = false;
			}
			// if this is a special token, don't keep it
			if (token.id >= 50256) {
				keep = false;
			}
			// if the second to last token is .id == 13 ('.'), don't keep it
			if (j == n_tokens - 2 && token.id == 13) {
				keep = false;
			}
			// token ids https://huggingface.co/openai/whisper-large-v3/raw/main/tokenizer.json
			if (token.id > 50365 && token.id <= 51865) {
				const float time = ((float)token.id - 50365.0f) * 0.02f;
				const float duration_s = (float)incoming_duration_ms / 1000.0f;
				const float ratio = time / duration_s;
				obs_log(gf->log_level,
					"Time token found %d -> %.3f. Duration: %.3f. Ratio: %.3f. Threshold %.2f",
					token.id, time, duration_s, ratio,
					gf->duration_filter_threshold);
				if (ratio > gf->duration_filter_threshold) {
					// ratio is too high, skip this detection
					obs_log(gf->log_level,
						"Time token ratio too high, skipping");
					return {DETECTION_RESULT_SILENCE, "", t0, t1, {}, language};
				}
				keep = false;
			}

			if (keep) {
				sentence_p += token.p;
				text += token_str;
				tokens.push_back(token);
			}
			obs_log(gf->log_level, "S %d, T %2d: %5d\t%s\tp: %.3f [keep: %d]",
				n_segment, j, token.id, token_str.c_str(), token.p, keep);
		}
	}
	sentence_p /= (float)tokens.size();
	if (sentence_p < gf->sentence_psum_accept_thresh) {
		obs_log(gf->log_level, "Sentence psum %.3f below threshold %.3f, skipping",
			sentence_p, gf->sentence_psum_accept_thresh);
		return {DETECTION_RESULT_SILENCE, "", t0, t1, {}, language};
	}

	obs_log(gf->log_level, "Decoded sentence: '%s'", text.c_str());

	if (gf->log_words) {
		obs_log(LOG_INFO, "[%s --> %s]%s(%.3f) %s", to_timestamp(t0).c_str(),
			to_timestamp(t1).c_str(), vad_state == VAD_STATE_PARTIAL ? "P" : " ",
			sentence_p, text.c_str());
	}

	if (text.empty() || text == "." || text == " " || text == "\n") {
		return {DETECTION_RESULT_SILENCE, "", t0, t1, {}, language};
	}

	return {vad_state == VAD_STATE_PARTIAL ? DETECTION_RESULT_PARTIAL : DETECTION_RESULT_SPEECH,
		text,
		t0,
		t1,
		tokens,
		language};
}

void run_inference_and_callbacks(transcription_filter_data *gf, uint64_t start_offset_ms,
				 uint64_t end_offset_ms, int vad_state)
{
	// get the data from the entire whisper buffer
	// add 50ms of silence to the beginning and end of the buffer
	const size_t pcm32f_size = gf->whisper_buffer.size / sizeof(float);
	const size_t pcm32f_size_with_silence = pcm32f_size + 2 * WHISPER_SAMPLE_RATE / 100;
	// allocate a new buffer and copy the data to it
	float *pcm32f_data = (float *)bzalloc(pcm32f_size_with_silence * sizeof(float));
	if (vad_state == VAD_STATE_PARTIAL) {
		// peek instead of pop, since this is a partial run that keeps the data in the buffer
		circlebuf_peek_front(&gf->whisper_buffer, pcm32f_data + WHISPER_SAMPLE_RATE / 100,
				     pcm32f_size * sizeof(float));
	} else {
		circlebuf_pop_front(&gf->whisper_buffer, pcm32f_data + WHISPER_SAMPLE_RATE / 100,
				    pcm32f_size * sizeof(float));
	}

	struct DetectionResultWithText inference_result =
		run_whisper_inference(gf, pcm32f_data, pcm32f_size_with_silence, start_offset_ms,
				      end_offset_ms, vad_state);
	// output inference result to a text source
	set_text_callback(gf, inference_result);

	if (gf->enable_audio_chunks_callback && vad_state != VAD_STATE_PARTIAL) {
		audio_chunk_callback(gf, pcm32f_data, pcm32f_size_with_silence, vad_state,
				     inference_result);
	}

	// free the buffer
	bfree(pcm32f_data);
}

void whisper_loop(void *data)
{
	if (data == nullptr) {
		obs_log(LOG_ERROR, "whisper_loop: data is null");
		return;
	}

	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	obs_log(gf->log_level, "Starting whisper thread");

	vad_state current_vad_state = {false, now_ms(), 0, 0};

	const char *whisper_loop_name = "Whisper loop";
	profile_register_root(whisper_loop_name, 50 * 1000 * 1000);

	// Thread main loop
	while (true) {
		ProfileScope(whisper_loop_name);
		{
			ProfileScope("lock whisper ctx");
			std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
			ProfileScope("locked whisper ctx");
			if (gf->whisper_context == nullptr) {
				obs_log(LOG_WARNING, "Whisper context is null, exiting thread");
				break;
			}
		}

		if (gf->vad_mode == VAD_MODE_HYBRID) {
			current_vad_state = hybrid_vad_segmentation(gf, current_vad_state);
		} else if (gf->vad_mode == VAD_MODE_ACTIVE) {
			current_vad_state = vad_based_segmentation(gf, current_vad_state);
		}

		if (!gf->cleared_last_sub) {
			// check if we should clear the current sub depending on the minimum subtitle duration
			uint64_t now = now_ms();
			if ((now - gf->last_sub_render_time) > gf->max_sub_duration) {
				// clear the current sub, call the callback with an empty string
				obs_log(gf->log_level,
					"Clearing current subtitle. now: %lu ms, last: %lu ms", now,
					gf->last_sub_render_time);
				clear_current_caption(gf);
			}
		}

		if (gf->input_cv.has_value())
			gf->input_cv->notify_one();

		// Sleep using the condition variable wshiper_thread_cv
		// This will wake up the thread if there is new data in the input buffer
		// or if the whisper context is null
		std::unique_lock<std::mutex> lock(gf->whisper_ctx_mutex);
		if (gf->input_buffers->size == 0) {
			gf->wshiper_thread_cv.wait_for(lock, std::chrono::milliseconds(50));
		}
	}

	obs_log(gf->log_level, "Exiting whisper thread");
}
