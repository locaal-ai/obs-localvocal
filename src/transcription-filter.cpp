#include <obs-module.h>
#include <obs-frontend-api.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <bitset>
#include <regex>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

#include <QString>

#include "plugin-support.h"
#include "transcription-filter.h"
#include "transcription-filter-callbacks.h"
#include "transcription-filter-data.h"
#include "transcription-filter-utils.h"
#include "transcription-utils.h"
#include "model-utils/model-downloader.h"
#include "whisper-utils/whisper-processing.h"
#include "whisper-utils/whisper-language.h"
#include "whisper-utils/whisper-model-utils.h"
#include "whisper-utils/whisper-utils.h"
#include "whisper-utils/whisper-params.h"
#include "translation/language_codes.h"
#include "translation/translation-utils.h"
#include "translation/translation.h"
#include "translation/translation-includes.h"
#include "ui/filter-replace-dialog.h"
#include "ui/filter-replace-utils.h"

void set_source_signals(transcription_filter_data *gf, obs_source_t *parent_source)
{
	signal_handler_t *sh = obs_source_get_signal_handler(parent_source);
	signal_handler_connect(sh, "media_play", media_play_callback, gf);
	signal_handler_connect(sh, "media_started", media_started_callback, gf);
	signal_handler_connect(sh, "media_pause", media_pause_callback, gf);
	signal_handler_connect(sh, "media_restart", media_restart_callback, gf);
	signal_handler_connect(sh, "media_stopped", media_stopped_callback, gf);
	gf->source_signals_set = true;
}

void disconnect_source_signals(transcription_filter_data *gf, obs_source_t *parent_source)
{
	signal_handler_t *sh = obs_source_get_signal_handler(parent_source);
	signal_handler_disconnect(sh, "media_play", media_play_callback, gf);
	signal_handler_disconnect(sh, "media_started", media_started_callback, gf);
	signal_handler_disconnect(sh, "media_pause", media_pause_callback, gf);
	signal_handler_disconnect(sh, "media_restart", media_restart_callback, gf);
	signal_handler_disconnect(sh, "media_stopped", media_stopped_callback, gf);
	gf->source_signals_set = false;
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

	// Lazy initialization of source signals
	if (!gf->source_signals_set) {
		// obs_filter_get_parent only works in the filter function
		obs_source_t *parent_source = obs_filter_get_parent(gf->context);
		if (parent_source != nullptr) {
			set_source_signals(gf, parent_source);
		}
	}

	if (!gf->active) {
		return audio;
	}

	if (gf->whisper_context == nullptr) {
		// Whisper not initialized, just pass through
		return audio;
	}

	// Check if process while muted is not enabled (e.g. the user wants to avoid processing audio
	// when the source is muted)
	if (!gf->process_while_muted) {
		// Check if the parent source is muted
		obs_source_t *parent_source = obs_filter_get_parent(gf->context);
		if (parent_source != nullptr && obs_source_muted(parent_source)) {
			// Source is muted, do not process audio
			return audio;
		}
	}

	{
		std::lock_guard<std::mutex> lock(gf->whisper_buf_mutex); // scoped lock
		// push back current audio data to input circlebuf
		for (size_t c = 0; c < gf->channels; c++) {
			circlebuf_push_back(&gf->input_buffers[c], audio->data[c],
					    audio->frames * sizeof(float));
		}
		// push audio packet info (timestamp/frame count) to info circlebuf
		struct transcription_filter_audio_info info = {0};
		info.frames = audio->frames; // number of frames in this packet
		// calculate timestamp offset from the start of the stream
		info.timestamp_offset_ns = now_ns() - gf->start_timestamp_ms * 1000000;
		circlebuf_push_back(&gf->info_buffer, &info, sizeof(info));
		gf->wshiper_thread_cv.notify_one();
	}

	return audio;
}

const char *transcription_filter_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return MT_("transcription_filterAudioFilter");
}

void transcription_filter_remove(void *data, obs_source_t *source)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	obs_log(gf->log_level, "filter remove");

	disconnect_source_signals(gf, source);
}

void transcription_filter_destroy(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	signal_handler_t *sh_filter = obs_source_get_signal_handler(gf->context);
	signal_handler_disconnect(sh_filter, "enable", enable_callback, gf);

	obs_log(gf->log_level, "filter destroy");
	shutdown_whisper_thread(gf);

	if (gf->resampler_to_whisper) {
		audio_resampler_destroy(gf->resampler_to_whisper);
	}

	{
		std::lock_guard<std::mutex> lockbuf(gf->whisper_buf_mutex);
		bfree(gf->copy_buffers[0]);
		gf->copy_buffers[0] = nullptr;
		for (size_t i = 0; i < gf->channels; i++) {
			circlebuf_free(&gf->input_buffers[i]);
		}
	}
	circlebuf_free(&gf->info_buffer);

	circlebuf_free(&gf->resampled_buffer);

	if (gf->captions_monitor.isEnabled()) {
		gf->captions_monitor.stopThread();
	}
	if (gf->translation_monitor.isEnabled()) {
		gf->translation_monitor.stopThread();
	}

	bfree(gf);
}

void transcription_filter_update(void *data, obs_data_t *s)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(gf->log_level, "LocalVocal filter update");

	gf->log_level = (int)obs_data_get_int(s, "log_level");
	gf->vad_mode = (int)obs_data_get_int(s, "vad_mode");
	gf->log_words = obs_data_get_bool(s, "log_words");
	gf->caption_to_stream = obs_data_get_bool(s, "caption_to_stream");
	gf->save_to_file = obs_data_get_bool(s, "file_output_enable");
	gf->save_srt = obs_data_get_bool(s, "subtitle_save_srt");
	gf->truncate_output_file = obs_data_get_bool(s, "truncate_output_file");
	gf->save_only_while_recording = obs_data_get_bool(s, "only_while_recording");
	gf->rename_file_to_match_recording = obs_data_get_bool(s, "rename_file_to_match_recording");
	// Get the current timestamp using the system clock
	gf->start_timestamp_ms = now_ms();
	gf->sentence_number = 1;
	gf->process_while_muted = obs_data_get_bool(s, "process_while_muted");
	gf->min_sub_duration = (int)obs_data_get_int(s, "min_sub_duration");
	gf->max_sub_duration = (int)obs_data_get_int(s, "max_sub_duration");
	gf->last_sub_render_time = now_ms();
	gf->duration_filter_threshold = (float)obs_data_get_double(s, "duration_filter_threshold");
	gf->segment_duration = (int)obs_data_get_int(s, "segment_duration");
	gf->partial_transcription = obs_data_get_bool(s, "partial_group");
	gf->partial_latency = (int)obs_data_get_int(s, "partial_latency");
	bool new_buffered_output = obs_data_get_bool(s, "buffered_output");
	int new_buffer_num_lines = (int)obs_data_get_int(s, "buffer_num_lines");
	int new_buffer_num_chars_per_line = (int)obs_data_get_int(s, "buffer_num_chars_per_line");
	TokenBufferSegmentation new_buffer_output_type =
		(TokenBufferSegmentation)obs_data_get_int(s, "buffer_output_type");
	const char *filter_words_replace = obs_data_get_string(s, "filter_words_replace");
	if (filter_words_replace != nullptr && strlen(filter_words_replace) > 0) {
		obs_log(gf->log_level, "filter_words_replace: %s", filter_words_replace);
		// deserialize the filter words replace
		gf->filter_words_replace = deserialize_filter_words_replace(filter_words_replace);
	} else {
		// clear the filter words replace
		gf->filter_words_replace.clear();
	}

	if (gf->save_to_file) {
		gf->output_file_path = "";
		// set the output file path
		const char *output_file_path = obs_data_get_string(s, "subtitle_output_filename");
		if (output_file_path != nullptr && strlen(output_file_path) > 0) {
			gf->output_file_path = output_file_path;
		} else {
			obs_log(gf->log_level, "output file path is empty, but selected to save");
		}
	}

	if (new_buffered_output) {
		obs_log(gf->log_level, "buffered_output enable");
		if (!gf->buffered_output || !gf->captions_monitor.isEnabled()) {
			obs_log(gf->log_level, "buffered_output currently disabled, enabling");
			gf->buffered_output = true;
			gf->captions_monitor.initialize(
				gf,
				[gf](const std::string &text) {
					if (gf->buffered_output) {
						send_caption_to_source(gf->text_source_name, text,
								       gf);
					}
				},
				[gf](const std::string &) {}, new_buffer_num_lines,
				new_buffer_num_chars_per_line, std::chrono::seconds(3),
				new_buffer_output_type);
			gf->translation_monitor.initialize(
				gf,
				[gf](const std::string &translated_text) {
					if (gf->buffered_output &&
					    gf->translation_output != "none") {
						send_caption_to_source(gf->translation_output,
								       translated_text, gf);
					}
				},
				[gf](const std::string &) {}, new_buffer_num_lines,
				new_buffer_num_chars_per_line, std::chrono::seconds(3),
				new_buffer_output_type);
		} else {
			if (new_buffer_num_lines != gf->buffered_output_num_lines ||
			    new_buffer_num_chars_per_line != gf->buffered_output_num_chars ||
			    new_buffer_output_type != gf->buffered_output_output_type) {
				obs_log(gf->log_level,
					"buffered_output parameters changed, updating");
				gf->captions_monitor.clear();
				gf->captions_monitor.setNumSentences(new_buffer_num_lines);
				gf->captions_monitor.setNumPerSentence(
					new_buffer_num_chars_per_line);
				gf->captions_monitor.setSegmentation(new_buffer_output_type);
				gf->translation_monitor.clear();
				gf->translation_monitor.setNumSentences(new_buffer_num_lines);
				gf->translation_monitor.setNumPerSentence(
					new_buffer_num_chars_per_line);
				gf->translation_monitor.setSegmentation(new_buffer_output_type);
			}
		}
		gf->buffered_output_num_lines = new_buffer_num_lines;
		gf->buffered_output_num_chars = new_buffer_num_chars_per_line;
		gf->buffered_output_output_type = new_buffer_output_type;
	} else {
		obs_log(gf->log_level, "buffered_output disable");
		if (gf->buffered_output) {
			obs_log(gf->log_level, "buffered_output currently enabled, disabling");
			if (gf->captions_monitor.isEnabled()) {
				gf->captions_monitor.clear();
				gf->captions_monitor.stopThread();
				gf->translation_monitor.clear();
				gf->translation_monitor.stopThread();
			}
			gf->buffered_output = false;
		}
	}

	bool new_translate = obs_data_get_bool(s, "translate");
	gf->target_lang = obs_data_get_string(s, "translate_target_language");
	gf->translation_ctx.add_context = (int)obs_data_get_int(s, "translate_add_context");
	gf->translation_ctx.input_tokenization_style =
		(InputTokenizationStyle)obs_data_get_int(s, "translate_input_tokenization_style");
	gf->translate_only_full_sentences = obs_data_get_bool(s, "translate_only_full_sentences");
	gf->translation_output = obs_data_get_string(s, "translate_output");
	std::string new_translate_model_index = obs_data_get_string(s, "translate_model");
	std::string new_translation_model_path_external =
		obs_data_get_string(s, "translation_model_path_external");

	if (new_translate) {
		if (new_translate != gf->translate ||
		    new_translate_model_index != gf->translation_model_index ||
		    new_translation_model_path_external != gf->translation_model_path_external) {
			// translation settings changed
			gf->translation_model_index = new_translate_model_index;
			gf->translation_model_path_external = new_translation_model_path_external;
			if (gf->translation_model_index != "whisper-based-translation") {
				start_translation(gf);
			} else {
				// whisper-based translation
				obs_log(gf->log_level, "Starting whisper-based translation...");
				gf->translate = false;
			}
		}
	} else {
		gf->translate = false;
	}

	// translation options
	if (gf->translate) {
		if (gf->translation_ctx.options) {
			gf->translation_ctx.options->sampling_temperature =
				(float)obs_data_get_double(s, "translation_sampling_temperature");
			gf->translation_ctx.options->repetition_penalty =
				(float)obs_data_get_double(s, "translation_repetition_penalty");
			gf->translation_ctx.options->beam_size =
				(int)obs_data_get_int(s, "translation_beam_size");
			gf->translation_ctx.options->max_decoding_length =
				(int)obs_data_get_int(s, "translation_max_decoding_length");
			gf->translation_ctx.options->no_repeat_ngram_size =
				(int)obs_data_get_int(s, "translation_no_repeat_ngram_size");
			gf->translation_ctx.options->max_input_length =
				(int)obs_data_get_int(s, "translation_max_input_length");
		}
	}

	gf->translate_cloud = obs_data_get_bool(s, "translate_cloud");
	gf->translate_cloud_config.provider = obs_data_get_string(s, "translate_cloud_provider");
	gf->translate_cloud_target_language =
		obs_data_get_string(s, "translate_cloud_target_language");
	gf->translate_cloud_output = obs_data_get_string(s, "translate_cloud_output");
	gf->translate_cloud_only_full_sentences =
		obs_data_get_bool(s, "translate_cloud_only_full_sentences");
	gf->translate_cloud_config.access_key = obs_data_get_string(s, "translate_cloud_api_key");
	gf->translate_cloud_config.secret_key =
		obs_data_get_string(s, "translate_cloud_secret_key");
	gf->translate_cloud_config.free = obs_data_get_bool(s, "translate_cloud_deepl_free");
	gf->translate_cloud_config.region = obs_data_get_string(s, "translate_cloud_region");
	gf->translate_cloud_config.endpoint = obs_data_get_string(s, "translate_cloud_endpoint");
	gf->translate_cloud_config.body = obs_data_get_string(s, "translate_cloud_body");
	gf->translate_cloud_config.response_json_path =
		obs_data_get_string(s, "translate_cloud_response_json_path");

	obs_log(gf->log_level, "update text source");
	// update the text source
	const char *new_text_source_name = obs_data_get_string(s, "subtitle_sources");

	if (new_text_source_name == nullptr || strcmp(new_text_source_name, "none") == 0 ||
	    strcmp(new_text_source_name, "(null)") == 0 || strlen(new_text_source_name) == 0) {
		// new selected text source is not valid, release the old one
		gf->text_source_name.clear();
	} else {
		gf->text_source_name = new_text_source_name;
	}

	obs_log(gf->log_level, "update whisper params");
	{
		std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);

		gf->n_context_sentences = (int)obs_data_get_int(s, "n_context_sentences");

		gf->sentence_psum_accept_thresh =
			(float)obs_data_get_double(s, "sentence_psum_accept_thresh");

		apply_whisper_params_from_settings(gf->whisper_params, s);

		if (!new_translate || gf->translation_model_index != "whisper-based-translation") {
			const char *whisper_language_select =
				obs_data_get_string(s, "whisper_language_select");
			const bool language_selected = whisper_language_select != nullptr &&
						       strlen(whisper_language_select) > 0;
			gf->whisper_params.language = (language_selected) ? whisper_language_select
									  : "auto";
			gf->whisper_params.detect_language = !language_selected;
		} else {
			// take the language from gf->target_lang
			if (language_codes_to_whisper.count(gf->target_lang) > 0) {
				gf->whisper_params.language =
					language_codes_to_whisper[gf->target_lang].c_str();
				gf->whisper_params.detect_language = false;
			} else {
				gf->whisper_params.language = "auto";
				gf->whisper_params.detect_language = true;
			}
		}

		if (gf->vad) {
			const float vad_threshold = (float)obs_data_get_double(s, "vad_threshold");
			gf->vad->set_threshold(vad_threshold);
		}
	}

	if (gf->context != nullptr && (obs_source_enabled(gf->context) || gf->initial_creation)) {
		if (gf->initial_creation) {
			obs_log(LOG_INFO, "Initial filter creation and source enabled");

			// source was enabled on creation
			update_whisper_model(gf);
			gf->active = true;
			gf->initial_creation = false;
		} else {
			// check if the whisper model selection has changed
			const std::string new_model_path =
				obs_data_get_string(s, "whisper_model_path") != nullptr
					? obs_data_get_string(s, "whisper_model_path")
					: "Whisper Tiny English (74Mb)";
			if (gf->whisper_model_path != new_model_path) {
				obs_log(LOG_INFO, "New model selected: %s", new_model_path.c_str());
				update_whisper_model(gf);
			}
		}
	} else {
		obs_log(LOG_INFO, "Filter not enabled, not updating whisper model.");
	}
}

void *transcription_filter_create(obs_data_t *settings, obs_source_t *filter)
{
	obs_log(LOG_INFO, "LocalVocal filter create");

	void *data = bmalloc(sizeof(struct transcription_filter_data));
	struct transcription_filter_data *gf = new (data) transcription_filter_data();

	// Get the number of channels for the input source
	gf->channels = audio_output_get_channels(obs_get_audio());
	gf->sample_rate = audio_output_get_sample_rate(obs_get_audio());
	gf->frames = (size_t)((float)gf->sample_rate / (1000.0f / MAX_MS_WORK_BUFFER));
	gf->last_num_frames = 0;
	gf->min_sub_duration = (int)obs_data_get_int(settings, "min_sub_duration");
	gf->max_sub_duration = (int)obs_data_get_int(settings, "max_sub_duration");
	gf->last_sub_render_time = now_ms();
	gf->log_level = (int)obs_data_get_int(settings, "log_level");
	gf->save_srt = obs_data_get_bool(settings, "subtitle_save_srt");
	gf->truncate_output_file = obs_data_get_bool(settings, "truncate_output_file");
	gf->save_only_while_recording = obs_data_get_bool(settings, "only_while_recording");
	gf->rename_file_to_match_recording =
		obs_data_get_bool(settings, "rename_file_to_match_recording");
	gf->process_while_muted = obs_data_get_bool(settings, "process_while_muted");
	gf->buffered_output = obs_data_get_bool(settings, "buffered_output");
	gf->initial_creation = true;

	for (size_t i = 0; i < gf->channels; i++) {
		circlebuf_init(&gf->input_buffers[i]);
	}
	circlebuf_init(&gf->info_buffer);
	circlebuf_init(&gf->whisper_buffer);
	circlebuf_init(&gf->resampled_buffer);

	// allocate copy buffers
	gf->copy_buffers[0] =
		static_cast<float *>(bzalloc(gf->channels * gf->frames * sizeof(float)));
	if (gf->copy_buffers[0] == nullptr) {
		obs_log(LOG_ERROR, "Failed to allocate copy buffer");
		gf->active = false;
		return nullptr;
	}
	for (size_t c = 1; c < gf->channels; c++) { // set the channel pointers
		gf->copy_buffers[c] = gf->copy_buffers[0] + c * gf->frames;
	}
	memset(gf->copy_buffers[0], 0, gf->channels * gf->frames * sizeof(float));

	gf->context = filter;

	obs_log(gf->log_level, "channels %d, frames %d, sample_rate %d", (int)gf->channels,
		(int)gf->frames, gf->sample_rate);

	obs_log(gf->log_level, "setup audio resampler");
	struct resample_info src, dst;
	src.samples_per_sec = gf->sample_rate;
	src.format = AUDIO_FORMAT_FLOAT_PLANAR;
	src.speakers = convert_speaker_layout((uint8_t)gf->channels);

	dst.samples_per_sec = WHISPER_SAMPLE_RATE;
	dst.format = AUDIO_FORMAT_FLOAT_PLANAR;
	dst.speakers = convert_speaker_layout((uint8_t)1);

	gf->resampler_to_whisper = audio_resampler_create(&dst, &src);
	if (!gf->resampler_to_whisper) {
		obs_log(LOG_ERROR, "Failed to create resampler");
		gf->active = false;
		return nullptr;
	}

	obs_log(gf->log_level, "clear text source data");
	const char *subtitle_sources = obs_data_get_string(settings, "subtitle_sources");
	if (subtitle_sources == nullptr || strlen(subtitle_sources) == 0 ||
	    strcmp(subtitle_sources, "none") == 0 || strcmp(subtitle_sources, "(null)") == 0) {
		obs_log(gf->log_level, "Create text source");
		create_obs_text_source_if_needed();
		gf->text_source_name = "LocalVocal Subtitles";
		obs_data_set_string(settings, "subtitle_sources", "LocalVocal Subtitles");
	} else {
		// set the text source name
		gf->text_source_name = subtitle_sources;
	}
	obs_log(gf->log_level, "clear paths and whisper context");
	gf->whisper_model_file_currently_loaded = "";
	gf->output_file_path = std::string("");
	gf->whisper_model_path = std::string(""); // The update function will set the model path
	gf->whisper_context = nullptr;

	signal_handler_t *sh_filter = obs_source_get_signal_handler(gf->context);
	if (sh_filter == nullptr) {
		obs_log(LOG_ERROR, "Failed to get signal handler");
		gf->active = false;
		return nullptr;
	}

	signal_handler_connect(sh_filter, "enable", enable_callback, gf);

	obs_log(gf->log_level, "run update");
	// get the settings updated on the filter data struct
	transcription_filter_update(gf, settings);

	// handle the event OBS_FRONTEND_EVENT_RECORDING_STARTING to reset the srt sentence number
	// to match the subtitles with the recording
	obs_frontend_add_event_callback(recording_state_callback, gf);

	obs_log(gf->log_level, "filter created.");
	return gf;
}

void transcription_filter_activate(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(gf->log_level, "filter activated");
	gf->active = true;
}

void transcription_filter_deactivate(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(gf->log_level, "filter deactivated");
	gf->active = false;
}

void transcription_filter_show(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(gf->log_level, "filter show");
}

void transcription_filter_hide(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(gf->log_level, "filter hide");
}
