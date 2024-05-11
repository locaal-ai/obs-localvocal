#include <whisper.h>

#include <obs-module.h>

#include "plugin-support.h"
#include "transcription-filter-data.h"
#include "whisper-processing.h"
#include "whisper-utils.h"
#include "transcription-utils.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>

#ifdef _WIN32
#include <fstream>
#include <Windows.h>
#endif
#include "model-utils/model-find-utils.h"

#define VAD_THOLD 0.0001f
#define FREQ_THOLD 100.0f

// Taken from https://github.com/ggerganov/whisper.cpp/blob/master/examples/stream/stream.cpp
std::string to_timestamp(int64_t t)
{
	int64_t sec = t / 1000;
	int64_t msec = t - sec * 1000;
	int64_t min = sec / 60;
	sec = sec - min * 60;

	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%02d.%03d", (int)min, (int)sec, (int)msec);

	return std::string(buf);
}

void high_pass_filter(float *pcmf32, size_t pcm32f_size, float cutoff, uint32_t sample_rate)
{
	const float rc = 1.0f / (2.0f * (float)M_PI * cutoff);
	const float dt = 1.0f / (float)sample_rate;
	const float alpha = dt / (rc + dt);

	float y = pcmf32[0];

	for (size_t i = 1; i < pcm32f_size; i++) {
		y = alpha * (y + pcmf32[i] - pcmf32[i - 1]);
		pcmf32[i] = y;
	}
}

float calculate_segment_energy(const float *pcmf32, size_t pcm32f_size)
{
	float energy = 0.0f;
	for (size_t i = 0; i < pcm32f_size; i++) {
		energy += fabsf(pcmf32[i]);
	}
	return energy / (float)pcm32f_size;
}

size_t find_tail_word_cutoff(const float *pcmf32, size_t pcm32f_size, size_t overlap_ms,
			     uint32_t sample_rate_hz)
{
	// segment size: 10ms worth of samples
	const size_t segment_size = 10 * sample_rate_hz / 1000;
	// overlap size in samples
	const size_t overlap_size = overlap_ms * sample_rate_hz / 1000;
	// tail lookup window starting point
	const size_t tail_lookup_start = pcm32f_size - overlap_size;

	size_t tail_word_cutoff = pcm32f_size;
	float lowest_energy = FLT_MAX;
	for (size_t i = tail_lookup_start; i < pcm32f_size - segment_size; i += segment_size / 2) {
		const float energy = calculate_segment_energy(pcmf32 + i, segment_size);
		if (energy < 0.0001 && energy < lowest_energy) {
			tail_word_cutoff = i;
			lowest_energy = energy;
		}
	}

	return tail_word_cutoff;
}

// VAD (voice activity detection), return true if speech detected
bool vad_simple(float *pcmf32, size_t pcm32f_size, uint32_t sample_rate, float vad_thold,
		float freq_thold, bool verbose)
{
	const uint64_t n_samples = pcm32f_size;

	if (freq_thold > 0.0f) {
		high_pass_filter(pcmf32, pcm32f_size, freq_thold, sample_rate);
	}

	float energy_all = 0.0f;

	for (uint64_t i = 0; i < n_samples; i++) {
		energy_all += fabsf(pcmf32[i]);
	}

	energy_all /= (float)n_samples;

	if (verbose) {
		obs_log(LOG_INFO, "%s: energy_all: %f, vad_thold: %f, freq_thold: %f", __func__,
			energy_all, vad_thold, freq_thold);
	}

	if (energy_all < vad_thold) {
		return false;
	}

	return true;
}

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
#elif defined(LOCALVOCAL_WITH_CLBLAST)
	cparams.use_gpu = true;
	obs_log(LOG_INFO, "Using OpenCL for inference");
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
						     const float *pcm32f_data_, size_t pcm32f_size_)
{
	if (gf == nullptr) {
		obs_log(LOG_ERROR, "run_whisper_inference: gf is null");
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0, {}};
	}

	if (pcm32f_data_ == nullptr || pcm32f_size_ == 0) {
		obs_log(LOG_ERROR, "run_whisper_inference: pcm32f_data is null or size is 0");
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0, {}};
	}

	obs_log(gf->log_level, "%s: processing %d samples, %.3f sec, %d threads", __func__,
		int(pcm32f_size_), float(pcm32f_size_) / WHISPER_SAMPLE_RATE,
		gf->whisper_params.n_threads);

	bool should_free_buffer = false;
	float *pcm32f_data = (float *)pcm32f_data_;
	size_t pcm32f_size = pcm32f_size_;

	if (pcm32f_size_ < WHISPER_SAMPLE_RATE) {
		obs_log(gf->log_level,
			"Speech segment is less than 1 second, padding with zeros to 1 second");
		const size_t new_size = (size_t)(1.01f * (float)(WHISPER_SAMPLE_RATE));
		// create a new buffer and copy the data to it
		pcm32f_data = (float *)bzalloc(new_size * sizeof(float));
		memset(pcm32f_data, 0, new_size * sizeof(float));
		memcpy(pcm32f_data, pcm32f_data_, pcm32f_size_ * sizeof(float));
		pcm32f_size = new_size;
		should_free_buffer = true;
	}

	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_WARNING, "whisper context is null");
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0, {}};
	}

	// Get the duration in ms since the beginning of the stream (gf->start_timestamp_ms)
	const uint64_t offset_ms =
		(uint64_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
				   .count() -
			   gf->start_timestamp_ms);

	// run the inference
	int whisper_full_result = -1;
	gf->whisper_params.duration_ms = (int)(pcm32f_size * 1000 / WHISPER_SAMPLE_RATE);
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
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0, {}};
	}
	if (should_free_buffer) {
		bfree(pcm32f_data);
	}

	if (whisper_full_result != 0) {
		obs_log(LOG_WARNING, "failed to process audio, error %d", whisper_full_result);
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0, {}};
	} else {
		// duration in ms
		const uint64_t duration_ms = (uint64_t)(pcm32f_size * 1000 / WHISPER_SAMPLE_RATE);

		const int n_segment = 0;
		// const char *text = whisper_full_get_segment_text(gf->whisper_context, n_segment);
		const int64_t t0 = offset_ms;
		const int64_t t1 = offset_ms + duration_ms;

		float sentence_p = 0.0f;
		const int n_tokens = whisper_full_n_tokens(gf->whisper_context, n_segment);
		std::string text = "";
		std::string tokenIds = "";
		std::vector<whisper_token_data> tokens;
		bool end = false;
		for (int j = 0; j < n_tokens; ++j) {
			// get token
			whisper_token_data token =
				whisper_full_get_token_data(gf->whisper_context, n_segment, j);
			sentence_p += token.p;
			const char *token_str = whisper_token_to_str(gf->whisper_context, token.id);
			bool keep = !end;
			// if the token starts with '[' and ends with ']', don't keep it
			if (token_str[0] == '[' && token_str[strlen(token_str) - 1] == ']') {
				keep = false;
			}
			// if this is a special token, don't keep it
			if (token.id >= 50256) {
				keep = false;
			}
			if (j == n_tokens - 2 && token.p < 0.5) {
				keep = false;
			}
			if (j == n_tokens - 3 && token.p < 0.4) {
				keep = false;
			}
			// if the second to last token is .id == 13 ('.'), don't keep it
			if (j == n_tokens - 2 && token.id == 13) {
				keep = false;
			}
			// token ids https://huggingface.co/openai/whisper-large-v3/raw/main/tokenizer.json
			// if (token.id > 50566 && token.id <= 51865) {
			// 	obs_log(gf->log_level,
			// 		"Large time token found (%d), this shouldn't happen",
			// 		token.id);
			// 	return {DETECTION_RESULT_UNKNOWN, "", 0, 0, {}};
			// }

			if (keep) {
				text += token_str;
				tokens.push_back(token);
			}
			obs_log(gf->log_level, "Token %d: %d\t%s\tp: %.3f [keep: %d]", j, token.id,
				std::string(token_str).c_str(), token.p, keep);
		}
		sentence_p /= (float)n_tokens;
		if (sentence_p < gf->sentence_psum_accept_thresh) {
			return {DETECTION_RESULT_SILENCE, "", 0, 0, {}};
		}

		obs_log(gf->log_level, "Decoded sentence: '%s'", text.c_str());

		if (gf->log_words) {
			obs_log(LOG_INFO, "[%s --> %s] (%.3f) %s", to_timestamp(t0).c_str(),
				to_timestamp(t1).c_str(), sentence_p, text.c_str());
		}

		if (text.empty() || text == ".") {
			return {DETECTION_RESULT_SILENCE, "", 0, 0, {}};
		}

		return {DETECTION_RESULT_SPEECH, text, offset_ms, offset_ms + duration_ms, tokens};
	}
}

void process_audio_from_buffer(struct transcription_filter_data *gf)
{
	uint32_t num_new_frames_from_infos = 0;
	uint64_t start_timestamp = 0;
	bool save_overlap_region = true;

	{
		// scoped lock the buffer mutex
		std::lock_guard<std::mutex> lock(gf->whisper_buf_mutex);

		// We need (gf->frames - gf->last_num_frames) new frames for a full segment,
		const size_t remaining_frames_to_full_segment = gf->frames - gf->last_num_frames;

		obs_log(gf->log_level,
			"processing audio from buffer, %lu existing frames, %lu frames needed to full segment (%d frames)",
			gf->last_num_frames, remaining_frames_to_full_segment, gf->frames);

		// pop infos from the info buffer and mark the beginning timestamp from the first
		// info as the beginning timestamp of the segment
		struct transcription_filter_audio_info info_from_buf = {0};
		const size_t size_of_audio_info = sizeof(struct transcription_filter_audio_info);
		while (gf->info_buffer.size >= size_of_audio_info) {
			circlebuf_pop_front(&gf->info_buffer, &info_from_buf, size_of_audio_info);
			num_new_frames_from_infos += info_from_buf.frames;
			if (start_timestamp == 0) {
				start_timestamp = info_from_buf.timestamp;
			}
			// Check if we're within the needed segment length
			if (num_new_frames_from_infos > remaining_frames_to_full_segment) {
				// too big, push the last info into the buffer's front where it was
				num_new_frames_from_infos -= info_from_buf.frames;
				circlebuf_push_front(&gf->info_buffer, &info_from_buf,
						     size_of_audio_info);
				break;
			}
		}

		obs_log(gf->log_level,
			"with %lu remaining to full segment, popped %d frames from info buffer, pushed at %lu (overlap)",
			remaining_frames_to_full_segment, num_new_frames_from_infos,
			gf->last_num_frames);

		/* Pop from input circlebuf */
		for (size_t c = 0; c < gf->channels; c++) {
			// Push the new data to the end of the existing buffer copy_buffers[c]
			circlebuf_pop_front(&gf->input_buffers[c],
					    gf->copy_buffers[c] + gf->last_num_frames,
					    num_new_frames_from_infos * sizeof(float));
		}
	}

	if (gf->last_num_frames > 0) {
		obs_log(gf->log_level, "full segment, %lu frames overlap, %lu frames to process",
			gf->last_num_frames, gf->last_num_frames + num_new_frames_from_infos);
		gf->last_num_frames += num_new_frames_from_infos;
	} else {
		gf->last_num_frames = num_new_frames_from_infos;
		obs_log(gf->log_level, "first segment, no overlap exists, %lu frames to process",
			gf->last_num_frames);
	}

	obs_log(gf->log_level, "processing %lu frames (%d ms), start timestamp %llu",
		gf->last_num_frames,
		(int)((float)gf->last_num_frames * 1000.0f / (float)gf->sample_rate),
		start_timestamp);

	// time the audio processing
	auto start = std::chrono::high_resolution_clock::now();

	// resample to 16kHz
	float *resampled_16khz[MAX_PREPROC_CHANNELS];
	uint32_t resampled_16khz_frames;
	uint64_t ts_offset;
	audio_resampler_resample(gf->resampler_to_whisper, (uint8_t **)resampled_16khz,
				 &resampled_16khz_frames, &ts_offset,
				 (const uint8_t **)gf->copy_buffers, (uint32_t)gf->last_num_frames);

	obs_log(gf->log_level, "%d channels, %d frames, %f ms", (int)gf->channels,
		(int)resampled_16khz_frames,
		(float)resampled_16khz_frames / WHISPER_SAMPLE_RATE * 1000.0f);

	bool skipped_inference = false;
	uint32_t speech_start_frame = 0;
	uint32_t speech_end_frame = resampled_16khz_frames;

	if (gf->vad_enabled) {
		std::vector<float> vad_input(resampled_16khz[0],
					     resampled_16khz[0] + resampled_16khz_frames);
		gf->vad->process(vad_input, false);

		std::vector<timestamp_t> stamps = gf->vad->get_speech_timestamps();
		if (stamps.size() == 0) {
			obs_log(gf->log_level, "VAD detected no speech in %d frames",
				resampled_16khz_frames);
			skipped_inference = true;
			// prevent copying the buffer to the beginning (overlap)
			save_overlap_region = false;
		} else {
			// if the vad finds that start within the first 10% of the buffer, set the start to 0
			speech_start_frame = (stamps[0].start < (int)(resampled_16khz_frames / 10))
						     ? 0
						     : stamps[0].start;
			speech_end_frame = stamps.back().end;
			uint32_t number_of_frames = speech_end_frame - speech_start_frame;

			// if the speech is pressed up against the end of the buffer
			// apply the overlapped region, else don't
			save_overlap_region = (speech_end_frame == resampled_16khz_frames);

			obs_log(gf->log_level,
				"VAD detected speech from %d to %d (%d frames, %d ms)",
				speech_start_frame, speech_end_frame, number_of_frames,
				number_of_frames * 1000 / WHISPER_SAMPLE_RATE);

			// if the speech is less than 1 second - pad with zeros and send for inference
			if (number_of_frames > 0 && number_of_frames < WHISPER_SAMPLE_RATE) {
				obs_log(gf->log_level,
					"Speech segment is less than 1 second, padding with zeros to 1 second");
				// copy the speech segment to the beginning of the resampled buffer
				// use memmove to copy the speech segment to the beginning of the buffer
				memmove(resampled_16khz[0], resampled_16khz[0] + speech_start_frame,
					number_of_frames * sizeof(float));
				// zero out the rest of the buffer
				memset(resampled_16khz[0] + number_of_frames, 0,
				       (WHISPER_SAMPLE_RATE - number_of_frames) * sizeof(float));

				speech_start_frame = 0;
				speech_end_frame = WHISPER_SAMPLE_RATE;
			}
		}
	}

	if (!skipped_inference) {
		// run inference
		const struct DetectionResultWithText inference_result =
			run_whisper_inference(gf, resampled_16khz[0] + speech_start_frame,
					      speech_end_frame - speech_start_frame);

		if (inference_result.result == DETECTION_RESULT_SPEECH) {
			// output inference result to a text source
			set_text_callback(gf, inference_result);
		} else if (inference_result.result == DETECTION_RESULT_SILENCE) {
			// output inference result to a text source
			set_text_callback(gf, {inference_result.result, "[silence]", 0, 0, {}});
		}
	} else {
		if (gf->log_words) {
			obs_log(LOG_INFO, "skipping inference");
		}
		set_text_callback(gf, {DETECTION_RESULT_UNKNOWN, "[skip]", 0, 0, {}});
	}

	// end of timer
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	const uint64_t last_num_frames_ms = gf->last_num_frames * 1000 / gf->sample_rate;
	obs_log(gf->log_level, "audio processing of %lu ms data took %d ms", last_num_frames_ms,
		(int)duration);

	if (save_overlap_region) {
		const uint64_t overlap_size_ms =
			(uint64_t)(gf->overlap_frames * 1000 / gf->sample_rate);
		obs_log(gf->log_level,
			"copying %lu overlap frames (%lu ms) from the end of the buffer (pos %lu) to the beginning",
			gf->overlap_frames, overlap_size_ms,
			gf->last_num_frames - gf->overlap_frames);
		for (size_t c = 0; c < gf->channels; c++) {
			// zero out the copy buffer, just in case
			memset(gf->copy_buffers[c], 0, gf->frames * sizeof(float));
			// move overlap frames from the end of the last copy_buffers to the beginning
			memmove(gf->copy_buffers[c],
				gf->copy_buffers[c] + gf->last_num_frames - gf->overlap_frames,
				gf->overlap_frames * sizeof(float));
		}
		gf->last_num_frames = gf->overlap_frames;
	} else {
		obs_log(gf->log_level, "no overlap needed. zeroing out the copy buffer");
		// zero out the copy buffer, just in case
		for (size_t c = 0; c < gf->channels; c++) {
			memset(gf->copy_buffers[c], 0, gf->frames * sizeof(float));
		}
		gf->last_num_frames = 0;
	}
}

void run_inference_and_callbak(transcription_filter_data *gf)
{
	struct DetectionResultWithText inference_result = run_whisper_inference(
		gf, (float *)gf->whisper_buffer.data, gf->whisper_buffer.size / sizeof(float));
	if (inference_result.result == DETECTION_RESULT_SPEECH) {
		// output inference result to a text source
		set_text_callback(gf, inference_result);
	} else if (inference_result.result == DETECTION_RESULT_SILENCE) {
		// output inference result to a text source
		set_text_callback(gf, {inference_result.result, "[silence]", 0, 0, {}});
	}
}

bool vad_based_segmentation(transcription_filter_data *gf, bool current_vad_on)
{
	uint32_t num_frames_from_infos = 0;
	uint64_t start_timestamp = 0;
	uint64_t end_timestamp = 0;
	// zero the copy_buffers
	for (size_t c = 0; c < gf->channels; c++) {
		memset(gf->copy_buffers[c], 0, gf->frames * sizeof(float));
	}

	{
		// scoped lock the buffer mutex
		std::lock_guard<std::mutex> lock(gf->whisper_buf_mutex);

		obs_log(gf->log_level,
			"vad based segmentation. currently %lu bytes in the audio input buffer",
			gf->input_buffers[0].size);

		// max number of frames is 10 seconds worth of audio
		const size_t max_num_frames = WHISPER_SAMPLE_RATE * 10;

		// pop all infos from the info buffer and mark the beginning timestamp from the first
		// info as the beginning timestamp of the segment
		struct transcription_filter_audio_info info_from_buf = {0};
		const size_t size_of_audio_info = sizeof(transcription_filter_audio_info);
		while (gf->info_buffer.size >= size_of_audio_info) {
			circlebuf_pop_front(&gf->info_buffer, &info_from_buf, size_of_audio_info);
			num_frames_from_infos += info_from_buf.frames;
			if (start_timestamp == 0) {
				start_timestamp = info_from_buf.timestamp;
			}
			// Check if we're within the needed segment length
			if (num_frames_from_infos > max_num_frames) {
				// too big, push the last info into the buffer's front where it was
				num_frames_from_infos -= info_from_buf.frames;
				circlebuf_push_front(&gf->info_buffer, &info_from_buf,
						     size_of_audio_info);
				break;
			}
		}
		end_timestamp = info_from_buf.timestamp;

		/* Pop from input circlebuf */
		for (size_t c = 0; c < gf->channels; c++) {
			// Push the new data to copy_buffers[c]
			circlebuf_pop_front(&gf->input_buffers[c], gf->copy_buffers[c],
					    num_frames_from_infos * sizeof(float));
		}
	}

	obs_log(gf->log_level, "found %d frames from info buffer", num_frames_from_infos);

	// resample to 16kHz
	float *resampled_16khz[MAX_PREPROC_CHANNELS];
	uint32_t resampled_16khz_frames;
	uint64_t ts_offset;
	audio_resampler_resample(gf->resampler_to_whisper, (uint8_t **)resampled_16khz,
				 &resampled_16khz_frames, &ts_offset,
				 (const uint8_t **)gf->copy_buffers,
				 (uint32_t)num_frames_from_infos);

	obs_log(gf->log_level, "resampled: %d channels, %d frames, %f ms", (int)gf->channels,
		(int)resampled_16khz_frames,
		(float)resampled_16khz_frames / WHISPER_SAMPLE_RATE * 1000.0f);

	std::vector<float> vad_input(resampled_16khz[0],
				     resampled_16khz[0] + resampled_16khz_frames);
	gf->vad->process(vad_input, false);

	std::vector<timestamp_t> stamps = gf->vad->get_speech_timestamps();
	if (stamps.size() == 0) {
		obs_log(gf->log_level, "VAD detected no speech in %d frames",
			resampled_16khz_frames);
		if (current_vad_on) {
			obs_log(gf->log_level, "VAD segment end - send to inference");
			current_vad_on = false;
			run_inference_and_callbak(gf);
			circlebuf_free(&gf->whisper_buffer);
		}
	} else {
		// process vad segments
		for (size_t i = 0; i < stamps.size(); i++) {
			int start_frame = stamps[i].start;
			if (i > 0) {
				start_frame = stamps[i - 1].end;
			}
			// push the data into gf-whisper_buffer
			circlebuf_push_back(&gf->whisper_buffer, resampled_16khz[0] + start_frame,
					    (stamps[i].end - start_frame) * sizeof(float));
			obs_log(gf->log_level,
				"VAD segment %d. pushed %d to %d (%d frames). current size: %lu bytes / %lu frames / %lu ms",
				i, start_frame, stamps[i].end, stamps[i].end - start_frame,
				gf->whisper_buffer.size, gf->whisper_buffer.size / sizeof(float),
				gf->whisper_buffer.size / sizeof(float) * 1000 /
					WHISPER_SAMPLE_RATE);

			if (stamps[i].end < (int)resampled_16khz_frames) {
				// VAD is currently on and there is a new "ending" segment
				// (not up to the end of the buffer)
				obs_log(gf->log_level, "VAD segment end - send to inference");
				current_vad_on = false;
				run_inference_and_callbak(gf);
				circlebuf_free(&gf->whisper_buffer);
			} else {
				current_vad_on = true;
			}
		}
	}

	return current_vad_on;
}

void whisper_loop(void *data)
{
	if (data == nullptr) {
		obs_log(LOG_ERROR, "whisper_loop: data is null");
		return;
	}

	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	obs_log(LOG_INFO, "starting whisper thread");

	bool current_vad_on = false;
	uint32_t min_num_bytes_for_vad = (500 * gf->sample_rate / 1000) * sizeof(float);

	// Thread main loop
	while (true) {
		{
			std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
			if (gf->whisper_context == nullptr) {
				obs_log(LOG_WARNING, "Whisper context is null, exiting thread");
				break;
			}
		}

		uint32_t num_bytes_on_input = 0;
		{
			// scoped lock the buffer mutex
			std::lock_guard<std::mutex> lock(gf->whisper_buf_mutex);
			num_bytes_on_input = (uint32_t)gf->input_buffers[0].size;
		}

		// only run vad segmentation if there are at least 500 ms of audio in the buffer
		if (num_bytes_on_input > min_num_bytes_for_vad) {
			current_vad_on = vad_based_segmentation(gf, current_vad_on);
		}

		// Sleep for 10 ms using the condition variable wshiper_thread_cv
		// This will wake up the thread if there is new data in the input buffer
		// or if the whisper context is null
		std::unique_lock<std::mutex> lock(gf->whisper_ctx_mutex);
		gf->wshiper_thread_cv.wait_for(lock, std::chrono::milliseconds(50));
	}

	obs_log(LOG_INFO, "exiting whisper thread");
}
