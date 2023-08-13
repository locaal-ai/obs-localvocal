#include <obs-module.h>

#include "plugin-support.h"
#include "transcription-filter.h"
#include "transcription-filter-data.h"
#include "whisper-processing.h"
#include "whisper-language.h"


inline enum speaker_layout convert_speaker_layout(uint8_t channels)
{
	switch (channels) {
	case 0:
		return SPEAKERS_UNKNOWN;
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_UNKNOWN;
	}
}

bool add_sources_to_list(void *list_property, obs_source_t *source)
{
	auto source_id = obs_source_get_id(source);
	if (strcmp(source_id, "text_ft2_source_v2") != 0 &&
	    strcmp(source_id, "text_gdiplus_v2") != 0) {
		return true;
	}

	obs_property_t *sources = (obs_property_t *)list_property;
	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(sources, name, name);
	return true;
}

struct obs_audio_data *transcription_filter_filter_audio(void *data, struct obs_audio_data *audio)
{
	if (!audio) {
		return nullptr;
	}
	if (data == nullptr) {
		return audio;
	}

	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	if (!gf->active) {
		return audio;
	}

	if (gf->whisper_context == nullptr) {
		// Whisper not initialized, just pass through
		return audio;
	}

	{
		std::lock_guard<std::mutex> lock(*gf->whisper_buf_mutex); // scoped lock
		obs_log(gf->log_level,
			"pushing %lu frames to input buffer. current size: %lu (bytes)",
			(size_t)(audio->frames), gf->input_buffers[0].size);
		// push back current audio data to input circlebuf
		for (size_t c = 0; c < gf->channels; c++) {
			circlebuf_push_back(&gf->input_buffers[c], audio->data[c],
					    audio->frames * sizeof(float));
		}
		// push audio packet info (timestamp/frame count) to info circlebuf
		struct transcription_filter_audio_info info = {0};
		info.frames = audio->frames;       // number of frames in this packet
		info.timestamp = audio->timestamp; // timestamp of this packet
		circlebuf_push_back(&gf->info_buffer, &info, sizeof(info));
	}

	return audio;
}

const char *transcription_filter_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return MT_("transcription_filterAudioFilter");
}

void transcription_filter_destroy(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	obs_log(LOG_INFO, "transcription_filter_destroy");
	{
		std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);
		if (gf->whisper_context != nullptr) {
			whisper_free(gf->whisper_context);
			gf->whisper_context = nullptr;
			gf->wshiper_thread_cv->notify_all();
		}
	}

	// join the thread
	if (gf->whisper_thread.joinable()) {
		gf->whisper_thread.join();
	}

	if (gf->text_source_name) {
		bfree(gf->text_source_name);
		gf->text_source_name = nullptr;
	}

	if (gf->text_source) {
		obs_weak_source_release(gf->text_source);
		gf->text_source = nullptr;
	}

	if (gf->resampler) {
		audio_resampler_destroy(gf->resampler);
	}

	{
		std::lock_guard<std::mutex> lockbuf(*gf->whisper_buf_mutex);
		bfree(gf->copy_buffers[0]);
		gf->copy_buffers[0] = nullptr;
		for (size_t i = 0; i < gf->channels; i++) {
			circlebuf_free(&gf->input_buffers[i]);
		}
	}
	circlebuf_free(&gf->info_buffer);

	bfree(gf);
}

void acquire_weak_text_source_ref(struct transcription_filter_data *gf)
{
	if (!gf->text_source_name) {
		obs_log(LOG_ERROR, "text_source_name is null");
		return;
	}

	std::lock_guard<std::mutex> lock(*gf->text_source_mutex);

	// acquire a weak ref to the new text source
	obs_source_t *source = obs_get_source_by_name(gf->text_source_name);
	if (source) {
		gf->text_source = obs_source_get_weak_source(source);
		obs_source_release(source);
		if (!gf->text_source) {
			obs_log(LOG_ERROR, "failed to get weak source for text source %s",
				gf->text_source_name);
		}
	} else {
		obs_log(LOG_ERROR, "text source '%s' not found", gf->text_source_name);
	}
}

void transcription_filter_update(void *data, obs_data_t *s)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	gf->filler_p_threshold = (float)obs_data_get_double(s, "filler_p_threshold");
	gf->log_level = (int)obs_data_get_int(s, "log_level");
	gf->do_silence = obs_data_get_bool(s, "do_silence");
	gf->vad_enabled = obs_data_get_bool(s, "vad_enabled");
	gf->log_words = obs_data_get_bool(s, "log_words");

	// update the text source
	const char *text_source_name = obs_data_get_string(s, "subtitle_sources");
	obs_weak_source_t *old_weak_text_source = NULL;

	if (strcmp(text_source_name, "none") == 0 || strcmp(text_source_name, "(null)") == 0) {
		// new selected text source is not valid, release the old one
		if (gf->text_source) {
			std::lock_guard<std::mutex> lock(*gf->text_source_mutex);
			old_weak_text_source = gf->text_source;
			gf->text_source = nullptr;
		}
		if (gf->text_source_name) {
			bfree(gf->text_source_name);
			gf->text_source_name = nullptr;
		}
	} else {
		// new selected text source is valid, check if it's different from the old one
		if (gf->text_source_name == nullptr ||
		    strcmp(text_source_name, gf->text_source_name) != 0) {
			// new text source is different from the old one, release the old one
			if (gf->text_source) {
				std::lock_guard<std::mutex> lock(*gf->text_source_mutex);
				old_weak_text_source = gf->text_source;
				gf->text_source = nullptr;
			}
			gf->text_source_name = bstrdup(text_source_name);
		}
	}

	if (old_weak_text_source) {
		obs_weak_source_release(old_weak_text_source);
	}

	const char *new_model_path = obs_data_get_string(s, "whisper_model_path");
	if (strcmp(new_model_path, gf->whisper_model_path.c_str()) != 0) {
		// model path changed, reload the model
		obs_log(LOG_INFO, "model path changed, reloading model");
		if (gf->whisper_context != nullptr) {
			// acquire the mutex before freeing the context
			std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);
			whisper_free(gf->whisper_context);
			gf->whisper_context = nullptr;
			gf->wshiper_thread_cv->notify_all();
		}
		if (gf->whisper_thread.joinable()) {
			gf->whisper_thread.join();
		}
		gf->whisper_model_path = bstrdup(new_model_path);

		// check if the model exists, if not, download it
		// if (!check_if_model_exists(gf->whisper_model_path)) {
		// 	obs_log(LOG_ERROR, "Whisper model does not exist");
		// download_model_with_ui_dialog(
		// 	gf->whisper_model_path, [gf](int download_status) {
		// 		if (download_status == 0) {
		// 			obs_log(LOG_INFO, "Model download complete");
		// 			gf->whisper_context = init_whisper_context(
		// 				gf->whisper_model_path);
		// 			gf->whisper_thread = std::thread(whisper_loop, gf);
		// 		} else {
		// 			obs_log(LOG_ERROR, "Model download failed");
		// 		}
		// 	});
		// } else {
		// Model exists, just load it
		gf->whisper_context = init_whisper_context(gf->whisper_model_path);
		gf->whisper_thread = std::thread(whisper_loop, gf);
		// }
	}

	std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);

	gf->whisper_params = whisper_full_default_params(
		(whisper_sampling_strategy)obs_data_get_int(s, "whisper_sampling_method"));
	gf->whisper_params.duration_ms = BUFFER_SIZE_MSEC;
	gf->whisper_params.language = obs_data_get_string(s, "whisper_language_select");
	gf->whisper_params.translate = false;
	gf->whisper_params.initial_prompt = obs_data_get_string(s, "initial_prompt");
	gf->whisper_params.n_threads = (int)obs_data_get_int(s, "n_threads");
	gf->whisper_params.n_max_text_ctx = (int)obs_data_get_int(s, "n_max_text_ctx");
	gf->whisper_params.no_context = obs_data_get_bool(s, "no_context");
	gf->whisper_params.single_segment = obs_data_get_bool(s, "single_segment");
	gf->whisper_params.print_special = obs_data_get_bool(s, "print_special");
	gf->whisper_params.print_progress = obs_data_get_bool(s, "print_progress");
	gf->whisper_params.print_realtime = obs_data_get_bool(s, "print_realtime");
	gf->whisper_params.print_timestamps = obs_data_get_bool(s, "print_timestamps");
	gf->whisper_params.token_timestamps = obs_data_get_bool(s, "token_timestamps");
	gf->whisper_params.thold_pt = (float)obs_data_get_double(s, "thold_pt");
	gf->whisper_params.thold_ptsum = (float)obs_data_get_double(s, "thold_ptsum");
	gf->whisper_params.max_len = (int)obs_data_get_int(s, "max_len");
	gf->whisper_params.split_on_word = obs_data_get_bool(s, "split_on_word");
	gf->whisper_params.max_tokens = (int)obs_data_get_int(s, "max_tokens");
	gf->whisper_params.speed_up = obs_data_get_bool(s, "speed_up");
	gf->whisper_params.suppress_blank = obs_data_get_bool(s, "suppress_blank");
	gf->whisper_params.suppress_non_speech_tokens =
		obs_data_get_bool(s, "suppress_non_speech_tokens");
	gf->whisper_params.temperature = (float)obs_data_get_double(s, "temperature");
	gf->whisper_params.max_initial_ts = (float)obs_data_get_double(s, "max_initial_ts");
	gf->whisper_params.length_penalty = (float)obs_data_get_double(s, "length_penalty");
}

void *transcription_filter_create(obs_data_t *settings, obs_source_t *filter)
{
	struct transcription_filter_data *gf = static_cast<struct transcription_filter_data *>(
		bmalloc(sizeof(struct transcription_filter_data)));

	// Get the number of channels for the input source
	gf->channels = audio_output_get_channels(obs_get_audio());
	gf->sample_rate = audio_output_get_sample_rate(obs_get_audio());
	gf->frames = (size_t)((float)gf->sample_rate / (1000.0f / (float)BUFFER_SIZE_MSEC));
	gf->last_num_frames = 0;

	for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		circlebuf_init(&gf->input_buffers[i]);
	}
	circlebuf_init(&gf->info_buffer);

	// allocate copy buffers
	gf->copy_buffers[0] =
		static_cast<float *>(bzalloc(gf->channels * gf->frames * sizeof(float)));
	for (size_t c = 1; c < gf->channels; c++) { // set the channel pointers
		gf->copy_buffers[c] = gf->copy_buffers[0] + c * gf->frames;
	}

	gf->context = filter;
	gf->whisper_model_path = obs_data_get_string(settings, "whisper_model_path");
	gf->whisper_context = init_whisper_context(gf->whisper_model_path);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_ERROR, "Failed to load whisper model");
		return nullptr;
	}

	gf->overlap_ms = OVERLAP_SIZE_MSEC;
	gf->overlap_frames = (size_t)((float)gf->sample_rate / (1000.0f / (float)gf->overlap_ms));
	obs_log(LOG_INFO, "transcription_filter filter: channels %d, frames %d, sample_rate %d",
		(int)gf->channels, (int)gf->frames, gf->sample_rate);

	struct resample_info src, dst;
	src.samples_per_sec = gf->sample_rate;
	src.format = AUDIO_FORMAT_FLOAT_PLANAR;
	src.speakers = convert_speaker_layout((uint8_t)gf->channels);

	dst.samples_per_sec = WHISPER_SAMPLE_RATE;
	dst.format = AUDIO_FORMAT_FLOAT_PLANAR;
	dst.speakers = convert_speaker_layout((uint8_t)1);

	gf->resampler = audio_resampler_create(&dst, &src);

	gf->active = true;

	gf->whisper_buf_mutex = std::unique_ptr<std::mutex>(new std::mutex());
	gf->whisper_ctx_mutex = std::unique_ptr<std::mutex>(new std::mutex());
	gf->wshiper_thread_cv =
		std::unique_ptr<std::condition_variable>(new std::condition_variable());
	gf->text_source_mutex = std::unique_ptr<std::mutex>(new std::mutex());

	// set the callback to set the text in the output text source (subtitles)
	gf->setTextCallback = [gf](const std::string &str) {
		if (!gf->text_source) {
			// attempt to acquire a weak ref to the text source if it's yet available
			acquire_weak_text_source_ref(gf);
		}

		std::lock_guard<std::mutex> lock(*gf->text_source_mutex);

		obs_weak_source_t *text_source = gf->text_source;
		if (!text_source) {
			obs_log(LOG_ERROR, "text_source is null");
			return;
		}
		auto target = obs_weak_source_get_source(text_source);
		if (!target) {
			obs_log(LOG_ERROR, "text_source target is null");
			return;
		}
		auto text_settings = obs_source_get_settings(target);
		obs_data_set_string(text_settings, "text", str.c_str());
		obs_source_update(target, text_settings);
		obs_source_release(target);
	};

	// get the settings updated on the filter data struct
	transcription_filter_update(gf, settings);

	// start the thread
	gf->whisper_thread = std::thread(whisper_loop, gf);

	return gf;
}

void transcription_filter_activate(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(LOG_INFO, "transcription_filter filter activated");
	gf->active = true;
}

void transcription_filter_deactivate(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(LOG_INFO, "transcription_filter filter deactivated");
	gf->active = false;
}

void transcription_filter_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "filler_p_threshold", 0.75);
	obs_data_set_default_bool(s, "do_silence", true);
	obs_data_set_default_bool(s, "vad_enabled", true);
	obs_data_set_default_int(s, "log_level", LOG_DEBUG);
	obs_data_set_default_bool(s, "log_words", true);
	obs_data_set_default_string(s, "whisper_model_path", "models/ggml-tiny.en.bin");
	obs_data_set_default_string(s, "whisper_language_select", "en");
	obs_data_set_default_string(s, "subtitle_sources", "none");

	// Whisper parameters
	obs_data_set_default_int(s, "whisper_sampling_method", WHISPER_SAMPLING_BEAM_SEARCH);
	obs_data_set_default_string(s, "initial_prompt", "");
	obs_data_set_default_int(s, "n_threads", 4);
	obs_data_set_default_int(s, "n_max_text_ctx", 16384);
	obs_data_set_default_bool(s, "no_context", true);
	obs_data_set_default_bool(s, "single_segment", true);
	obs_data_set_default_bool(s, "print_special", false);
	obs_data_set_default_bool(s, "print_progress", false);
	obs_data_set_default_bool(s, "print_realtime", false);
	obs_data_set_default_bool(s, "print_timestamps", false);
	obs_data_set_default_bool(s, "token_timestamps", false);
	obs_data_set_default_double(s, "thold_pt", 0.01);
	obs_data_set_default_double(s, "thold_ptsum", 0.01);
	obs_data_set_default_int(s, "max_len", 0);
	obs_data_set_default_bool(s, "split_on_word", false);
	obs_data_set_default_int(s, "max_tokens", 32);
	obs_data_set_default_bool(s, "speed_up", false);
	obs_data_set_default_bool(s, "suppress_blank", false);
	obs_data_set_default_bool(s, "suppress_non_speech_tokens", true);
	obs_data_set_default_double(s, "temperature", 0.5);
	obs_data_set_default_double(s, "max_initial_ts", 1.0);
	obs_data_set_default_double(s, "length_penalty", -1.0);
}

obs_properties_t *transcription_filter_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_add_float_slider(ppts, "filler_p_threshold", "filler_p_threshold", 0.0f,
					1.0f, 0.05f);
	obs_properties_add_bool(ppts, "do_silence", "do_silence");
	obs_properties_add_bool(ppts, "vad_enabled", "vad_enabled");
	obs_property_t *list = obs_properties_add_list(ppts, "log_level", "log_level",
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, "DEBUG", LOG_DEBUG);
	obs_property_list_add_int(list, "INFO", LOG_INFO);
	obs_property_list_add_int(list, "WARNING", LOG_WARNING);
	obs_properties_add_bool(ppts, "log_words", "log_words");

	obs_property_t *sources = obs_properties_add_list(ppts, "subtitle_sources",
							  "subtitle_sources", OBS_COMBO_TYPE_LIST,
							  OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(add_sources_to_list, sources);

	// Add a list of available whisper models to download
	obs_property_t *whisper_models_list =
		obs_properties_add_list(ppts, "whisper_model_path", "Whisper Model",
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(whisper_models_list, "Tiny (Eng) 75Mb",
				     "models/ggml-tiny.en.bin");
	obs_property_list_add_string(whisper_models_list, "Tiny 75Mb", "models/ggml-tiny.bin");
	obs_property_list_add_string(whisper_models_list, "Base (Eng) 142Mb",
				     "models/ggml-base.en.bin");
	obs_property_list_add_string(whisper_models_list, "Base 142Mb", "models/ggml-base.bin");
	obs_property_list_add_string(whisper_models_list, "Small (Eng) 466Mb",
				     "models/ggml-small.en.bin");
	obs_property_list_add_string(whisper_models_list, "Small 466Mb", "models/ggml-small.bin");

	obs_properties_t *whisper_params_group = obs_properties_create();
	obs_properties_add_group(ppts, "whisper_params_group", "Whisper Parameters",
				 OBS_GROUP_NORMAL, whisper_params_group);

	// Add language selector
	obs_property_t *whisper_language_select_list =
		obs_properties_add_list(whisper_params_group, "whisper_language_select", "Language",
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// iterate over all available languages in whisper_available_lang map<string, string>
	for (auto const &pair : whisper_available_lang) {
		obs_property_list_add_string(whisper_language_select_list, pair.second.c_str(),
					     pair.first.c_str());
	}

	obs_property_t *whisper_sampling_method_list = obs_properties_add_list(
		whisper_params_group, "whisper_sampling_method", "whisper_sampling_method",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(whisper_sampling_method_list, "Beam search",
				  WHISPER_SAMPLING_BEAM_SEARCH);
	obs_property_list_add_int(whisper_sampling_method_list, "Greedy", WHISPER_SAMPLING_GREEDY);

	// int n_threads;
	obs_properties_add_int_slider(whisper_params_group, "n_threads", "n_threads", 1, 8, 1);
	// int n_max_text_ctx;     // max tokens to use from past text as prompt for the decoder
	obs_properties_add_int_slider(whisper_params_group, "n_max_text_ctx", "n_max_text_ctx", 0,
				      16384, 100);
	// int offset_ms;          // start offset in ms
	// int duration_ms;        // audio duration to process in ms
	// bool translate;
	// bool no_context;        // do not use past transcription (if any) as initial prompt for the decoder
	obs_properties_add_bool(whisper_params_group, "no_context", "no_context");
	// bool single_segment;    // force single segment output (useful for streaming)
	obs_properties_add_bool(whisper_params_group, "single_segment", "single_segment");
	// bool print_special;     // print special tokens (e.g. <SOT>, <EOT>, <BEG>, etc.)
	obs_properties_add_bool(whisper_params_group, "print_special", "print_special");
	// bool print_progress;    // print progress information
	obs_properties_add_bool(whisper_params_group, "print_progress", "print_progress");
	// bool print_realtime;    // print results from within whisper.cpp (avoid it, use callback instead)
	obs_properties_add_bool(whisper_params_group, "print_realtime", "print_realtime");
	// bool print_timestamps;  // print timestamps for each text segment when printing realtime
	obs_properties_add_bool(whisper_params_group, "print_timestamps", "print_timestamps");
	// bool  token_timestamps; // enable token-level timestamps
	obs_properties_add_bool(whisper_params_group, "token_timestamps", "token_timestamps");
	// float thold_pt;         // timestamp token probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_pt", "thold_pt", 0.0f, 1.0f,
					0.05f);
	// float thold_ptsum;      // timestamp token sum probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_ptsum", "thold_ptsum", 0.0f,
					1.0f, 0.05f);
	// int   max_len;          // max segment length in characters
	obs_properties_add_int_slider(whisper_params_group, "max_len", "max_len", 0, 100, 1);
	// bool  split_on_word;    // split on word rather than on token (when used with max_len)
	obs_properties_add_bool(whisper_params_group, "split_on_word", "split_on_word");
	// int   max_tokens;       // max tokens per segment (0 = no limit)
	obs_properties_add_int_slider(whisper_params_group, "max_tokens", "max_tokens", 0, 100, 1);
	// bool speed_up;          // speed-up the audio by 2x using Phase Vocoder
	obs_properties_add_bool(whisper_params_group, "speed_up", "speed_up");
	// const char * initial_prompt;
	obs_properties_add_text(whisper_params_group, "initial_prompt", "initial_prompt",
				OBS_TEXT_DEFAULT);
	// bool suppress_blank
	obs_properties_add_bool(whisper_params_group, "suppress_blank", "suppress_blank");
	// bool suppress_non_speech_tokens
	obs_properties_add_bool(whisper_params_group, "suppress_non_speech_tokens",
				"suppress_non_speech_tokens");
	// float temperature
	obs_properties_add_float_slider(whisper_params_group, "temperature", "temperature", 0.0f,
					1.0f, 0.05f);
	// float max_initial_ts
	obs_properties_add_float_slider(whisper_params_group, "max_initial_ts", "max_initial_ts",
					0.0f, 1.0f, 0.05f);
	// float length_penalty
	obs_properties_add_float_slider(whisper_params_group, "length_penalty", "length_penalty",
					-1.0f, 1.0f, 0.1f);

	UNUSED_PARAMETER(data);
	return ppts;
}
