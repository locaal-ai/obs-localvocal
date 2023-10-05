#include <whisper.h>

#include <obs-module.h>

#include "plugin-support.h"
#include "transcription-filter-data.h"
#include "whisper-processing.h"

#include <algorithm>
#include <cctype>

#define VAD_THOLD 0.0001f
#define FREQ_THOLD 100.0f

// Taken from https://github.com/ggerganov/whisper.cpp/blob/master/examples/stream/stream.cpp
std::string to_timestamp(int64_t t)
{
	int64_t sec = t / 100;
	int64_t msec = t - sec * 100;
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

struct whisper_context *init_whisper_context(const std::string &model_path)
{
	obs_log(LOG_INFO, "Loading whisper model from %s", model_path.c_str());
	struct whisper_context *ctx = whisper_init_from_file(model_path.c_str());
	if (ctx == nullptr) {
		obs_log(LOG_ERROR, "Failed to load whisper model");
		return nullptr;
	}
	obs_log(LOG_INFO, "Whisper model loaded: %s", whisper_print_system_info());
	return ctx;
}

enum DetectionResult {
	DETECTION_RESULT_UNKNOWN = 0,
	DETECTION_RESULT_SILENCE = 1,
	DETECTION_RESULT_SPEECH = 2,
};

struct DetectionResultWithText {
	DetectionResult result;
	std::string text;
};

struct DetectionResultWithText run_whisper_inference(struct transcription_filter_data *gf,
						     const float *pcm32f_data, size_t pcm32f_size)
{
	obs_log(gf->log_level, "%s: processing %d samples, %.3f sec, %d threads", __func__,
		int(pcm32f_size), float(pcm32f_size) / WHISPER_SAMPLE_RATE,
		gf->whisper_params.n_threads);

	std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_WARNING, "whisper context is null");
		return {DETECTION_RESULT_UNKNOWN, ""};
	}

	// run the inference
	int whisper_full_result = -1;
	try {
		whisper_full_result = whisper_full(gf->whisper_context, gf->whisper_params,
						   pcm32f_data, (int)pcm32f_size);
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Whisper exception: %s. Filter restart is required", e.what());
		whisper_free(gf->whisper_context);
		gf->whisper_context = nullptr;
		return {DETECTION_RESULT_UNKNOWN, ""};
	}

	if (whisper_full_result != 0) {
		obs_log(LOG_WARNING, "failed to process audio, error %d", whisper_full_result);
		return {DETECTION_RESULT_UNKNOWN, ""};
	} else {
		const int n_segment = 0;
		const char *text = whisper_full_get_segment_text(gf->whisper_context, n_segment);
		const int64_t t0 = whisper_full_get_segment_t0(gf->whisper_context, n_segment);
		const int64_t t1 = whisper_full_get_segment_t1(gf->whisper_context, n_segment);

		float sentence_p = 0.0f;
		const int n_tokens = whisper_full_n_tokens(gf->whisper_context, n_segment);
		for (int j = 0; j < n_tokens; ++j) {
			sentence_p += whisper_full_get_token_p(gf->whisper_context, n_segment, j);
		}
		sentence_p /= (float)n_tokens;

		// convert text to lowercase
		std::string text_lower(text);
		std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
		// trim whitespace (use lambda)
		text_lower.erase(std::find_if(text_lower.rbegin(), text_lower.rend(),
					      [](unsigned char ch) { return !std::isspace(ch); })
					 .base(),
				 text_lower.end());

		if (gf->log_words) {
			obs_log(LOG_INFO, "[%s --> %s] (%.3f) %s", to_timestamp(t0).c_str(),
				to_timestamp(t1).c_str(), sentence_p, text_lower.c_str());
		}

		if (text_lower.empty() || text_lower == ".") {
			return {DETECTION_RESULT_SILENCE, ""};
		}

		return {DETECTION_RESULT_SPEECH, text_lower};
	}
}

void process_audio_from_buffer(struct transcription_filter_data *gf)
{
	uint32_t num_new_frames_from_infos = 0;
	uint64_t start_timestamp = 0;
	bool last_step_in_segment = false;

	{
		// scoped lock the buffer mutex
		std::lock_guard<std::mutex> lock(*gf->whisper_buf_mutex);

		// We need (gf->frames - gf->last_num_frames) new frames for a full segment,
		const size_t remaining_frames_to_full_segment = gf->frames - gf->last_num_frames;

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
				last_step_in_segment =
					true; // this is the final step in the segment
				break;
			}
		}

		obs_log(gf->log_level,
			"with %lu remaining to full segment, popped %d info-frames, pushing into buffer at %lu",
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
		gf->last_num_frames += num_new_frames_from_infos;
		if (!last_step_in_segment) {
			// Mid-segment process
			obs_log(gf->log_level, "mid-segment, now %d frames left to full segment",
				(int)(gf->frames - gf->last_num_frames));
		} else {
			// Final step in segment
			obs_log(gf->log_level, "full segment, %d frames to process",
				(int)(gf->last_num_frames));
		}
	} else {
		gf->last_num_frames = num_new_frames_from_infos;
		obs_log(gf->log_level, "first segment, %d frames to process",
			(int)(gf->last_num_frames));
	}

	obs_log(gf->log_level, "processing %d frames (%d ms), start timestamp %llu ",
		(int)gf->last_num_frames, (int)(gf->last_num_frames * 1000 / gf->sample_rate),
		start_timestamp);

	// time the audio processing
	auto start = std::chrono::high_resolution_clock::now();

	// resample to 16kHz
	float *output[MAX_PREPROC_CHANNELS];
	uint32_t out_frames;
	uint64_t ts_offset;
	audio_resampler_resample(gf->resampler, (uint8_t **)output, &out_frames, &ts_offset,
				 (const uint8_t **)gf->copy_buffers, (uint32_t)gf->last_num_frames);

	obs_log(gf->log_level, "%d channels, %d frames, %f ms", (int)gf->channels, (int)out_frames,
		(float)out_frames / WHISPER_SAMPLE_RATE * 1000.0f);

	bool skipped_inference = false;

	if (gf->vad_enabled) {
		skipped_inference = !::vad_simple(output[0], out_frames, WHISPER_SAMPLE_RATE,
						  VAD_THOLD, FREQ_THOLD,
						  gf->log_level != LOG_DEBUG);
	}

	if (!skipped_inference) {
		// run inference
		const struct DetectionResultWithText inference_result =
			run_whisper_inference(gf, output[0], out_frames);

		if (inference_result.result == DETECTION_RESULT_SPEECH) {
			// output inference result to a text source
			set_text_callback(gf, inference_result.text);
		} else if (inference_result.result == DETECTION_RESULT_SILENCE) {
			// output inference result to a text source
			set_text_callback(gf, "[silence]");
		}
	} else {
		if (gf->log_words) {
			obs_log(LOG_INFO, "skipping inference");
		}
		set_text_callback(gf, "");
	}

	// end of timer
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	const uint64_t last_num_frames_ms = gf->last_num_frames * 1000 / gf->sample_rate;
	obs_log(gf->log_level, "audio processing of %lu ms data took %d ms", last_num_frames_ms,
		(int)duration);

	if (last_step_in_segment) {
		for (size_t c = 0; c < gf->channels; c++) {
			// This is the last step in the segment - reset the copy buffer (include overlap frames)
			// move overlap frames from the end of the last copy_buffers to the beginning
			memcpy(gf->copy_buffers[c],
			       gf->copy_buffers[c] + gf->last_num_frames - gf->overlap_frames,
			       gf->overlap_frames * sizeof(float));
			// zero out the rest of the buffer, just in case
			memset(gf->copy_buffers[c] + gf->overlap_frames, 0,
			       (gf->frames - gf->overlap_frames) * sizeof(float));
			gf->last_num_frames = gf->overlap_frames;
		}
	}
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

	// Thread main loop
	while (true) {
		{
			std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);
			if (gf->whisper_context == nullptr) {
				obs_log(LOG_WARNING, "Whisper context is null, exiting thread");
				break;
			}
		}

		// Check if we have enough data to process
		while (true) {
			size_t input_buf_size = 0;
			{
				std::lock_guard<std::mutex> lock(*gf->whisper_buf_mutex);
				input_buf_size = gf->input_buffers[0].size;
			}
			const size_t step_size_frames = gf->step_size_msec * gf->sample_rate / 1000;
			const size_t segment_size = step_size_frames * sizeof(float);

			if (input_buf_size >= segment_size) {
				obs_log(gf->log_level,
					"found %lu bytes, %lu frames in input buffer, need >= %lu, processing",
					input_buf_size, (size_t)(input_buf_size / sizeof(float)),
					segment_size);

				// Process the audio. This will also remove the processed data from the input buffer.
				// Mutex is locked inside process_audio_from_buffer.
				process_audio_from_buffer(gf);
			} else {
				break;
			}
		}
		// Sleep for 10 ms using the condition variable wshiper_thread_cv
		// This will wake up the thread if there is new data in the input buffer
		// or if the whisper context is null
		std::unique_lock<std::mutex> lock(*gf->whisper_ctx_mutex);
		gf->wshiper_thread_cv->wait_for(lock, std::chrono::milliseconds(10));
	}

	obs_log(LOG_INFO, "exiting whisper thread");
}
