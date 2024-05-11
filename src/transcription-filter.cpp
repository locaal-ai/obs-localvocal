#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"
#include "transcription-filter.h"
#include "transcription-filter-data.h"
#include "transcription-utils.h"
#include "model-utils/model-downloader.h"
#include "whisper-utils/whisper-processing.h"
#include "whisper-utils/whisper-language.h"
#include "whisper-utils/whisper-model-utils.h"
#include "whisper-utils/whisper-utils.h"
#include "translation/language_codes.h"
#include "translation/translation-utils.h"
#include "translation/translation.h"
#include "utils.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <bitset>
#ifdef _WIN32
#include <Windows.h>
#endif

#include <QString>

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

	// Check if the parent source is muted
	obs_source_t *parent_source = obs_filter_get_parent(gf->context);
	if (gf->process_while_muted == false && obs_source_muted(parent_source)) {
		// Source is muted, do not process audio
		return audio;
	}

	if (gf->whisper_context == nullptr) {
		// Whisper not initialized, just pass through
		return audio;
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

	bfree(gf);
}

void send_caption_to_source(const std::string &target_source_name, const std::string &str_copy,
			    struct transcription_filter_data *gf)
{
	auto target = obs_get_source_by_name(target_source_name.c_str());
	if (!target) {
		obs_log(gf->log_level, "text_source target is null");
		return;
	}
	auto text_settings = obs_source_get_settings(target);
	obs_data_set_string(text_settings, "text", str_copy.c_str());
	obs_source_update(target, text_settings);
	obs_source_release(target);
}

void set_text_callback(struct transcription_filter_data *gf,
		       const DetectionResultWithText &resultIn)
{
	DetectionResultWithText result = resultIn;
	uint64_t now = now_ms();
	if (result.text.empty() || result.result != DETECTION_RESULT_SPEECH) {
		// check if we should clear the current sub depending on the minimum subtitle duration
		if ((now - gf->last_sub_render_time) > gf->min_sub_duration) {
			// clear the current sub, run an empty sub
			result.text = "";
		} else {
			// nothing to do, the incoming sub is empty
			return;
		}
	}
	gf->last_sub_render_time = now;

	std::string str_copy = result.text;

	// recondition the text - only if the output is not English
	if (gf->whisper_params.language != nullptr &&
	    strcmp(gf->whisper_params.language, "en") != 0) {
		str_copy = fix_utf8(str_copy);
	} else {
		str_copy = remove_leading_trailing_nonalpha(str_copy);
	}

	// if suppression is enabled, check if the text is in the suppression list
	if (!gf->suppress_sentences.empty()) {
		// split the suppression list by newline into individual sentences
		std::vector<std::string> suppress_sentences_list =
			split(gf->suppress_sentences, '\n');
		// check if the text is in the suppression list
		for (const std::string &suppress_sentence : suppress_sentences_list) {
			if (str_copy == suppress_sentence) {
				obs_log(gf->log_level, "Suppressed sentence: '%s'",
					str_copy.c_str());
				gf->last_text = str_copy;
				return; // do not process the sentence
			}
		}
	}

	if (gf->translate && !str_copy.empty() && str_copy != gf->last_text &&
	    result.result == DETECTION_RESULT_SPEECH) {
		obs_log(gf->log_level, "Translating text. %s -> %s", gf->source_lang.c_str(),
			gf->target_lang.c_str());
		std::string translated_text;
		if (translate(gf->translation_ctx, str_copy, gf->source_lang, gf->target_lang,
			      translated_text) == OBS_POLYGLOT_TRANSLATION_SUCCESS) {
			if (gf->log_words) {
				obs_log(LOG_INFO, "Translation: '%s' -> '%s'", str_copy.c_str(),
					translated_text.c_str());
			}
			if (gf->translation_output == "none") {
				// overwrite the original text with the translated text
				str_copy = translated_text;
			} else {
				// send the translation to the selected source
				send_caption_to_source(gf->translation_output, translated_text, gf);
			}
		} else {
			obs_log(gf->log_level, "Failed to translate text");
		}
	}

	gf->last_text = str_copy;

	if (gf->buffered_output) {
		gf->captions_monitor.addWords(result.tokens);
	}

	if (gf->caption_to_stream) {
		obs_output_t *streaming_output = obs_frontend_get_streaming_output();
		if (streaming_output) {
			obs_output_output_caption_text1(streaming_output, str_copy.c_str());
			obs_output_release(streaming_output);
		}
	}

	if (gf->output_file_path != "" && gf->text_source_name.empty()) {
		// Check if we should save the sentence
		if (gf->save_only_while_recording && !obs_frontend_recording_active()) {
			// We are not recording, do not save the sentence to file
			return;
		}
		// should the file be truncated?
		std::ios_base::openmode openmode = std::ios::out;
		if (gf->truncate_output_file) {
			openmode |= std::ios::trunc;
		} else {
			openmode |= std::ios::app;
		}
		if (!gf->save_srt) {
			// Write raw sentence to file
			std::ofstream output_file(gf->output_file_path, openmode);
			output_file << str_copy << std::endl;
			output_file.close();
		} else {
			obs_log(gf->log_level, "Saving sentence to file %s, sentence #%d",
				gf->output_file_path.c_str(), gf->sentence_number);
			// Append sentence to file in .srt format
			std::ofstream output_file(gf->output_file_path, openmode);
			output_file << gf->sentence_number << std::endl;
			// use the start and end timestamps to calculate the start and end time in srt format
			auto format_ts_for_srt = [&output_file](uint64_t ts) {
				uint64_t time_s = ts / 1000;
				uint64_t time_m = time_s / 60;
				uint64_t time_h = time_m / 60;
				uint64_t time_ms_rem = ts % 1000;
				uint64_t time_s_rem = time_s % 60;
				uint64_t time_m_rem = time_m % 60;
				uint64_t time_h_rem = time_h % 60;
				output_file << std::setfill('0') << std::setw(2) << time_h_rem
					    << ":" << std::setfill('0') << std::setw(2)
					    << time_m_rem << ":" << std::setfill('0')
					    << std::setw(2) << time_s_rem << ","
					    << std::setfill('0') << std::setw(3) << time_ms_rem;
			};
			format_ts_for_srt(result.start_timestamp_ms);
			output_file << " --> ";
			format_ts_for_srt(result.end_timestamp_ms);
			output_file << std::endl;

			output_file << str_copy << std::endl;
			output_file << std::endl;
			output_file.close();
			gf->sentence_number++;
		}
	} else {
		if (!gf->buffered_output) {
			// Send the caption to the text source
			send_caption_to_source(gf->text_source_name, str_copy, gf);
		}
	}
};

void transcription_filter_update(void *data, obs_data_t *s)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	gf->log_level = (int)obs_data_get_int(s, "log_level");
	obs_log(gf->log_level, "filter update");

	gf->vad_enabled = obs_data_get_bool(s, "vad_enabled");
	gf->log_words = obs_data_get_bool(s, "log_words");
	gf->frames = (size_t)((float)gf->sample_rate /
			      (1000.0f / (float)obs_data_get_int(s, "buffer_size_msec")));
	gf->caption_to_stream = obs_data_get_bool(s, "caption_to_stream");
	bool step_by_step_processing = obs_data_get_bool(s, "step_by_step_processing");
	gf->step_size_msec = step_by_step_processing ? (int)obs_data_get_int(s, "step_size_msec")
						     : obs_data_get_int(s, "buffer_size_msec");
	gf->save_srt = obs_data_get_bool(s, "subtitle_save_srt");
	gf->truncate_output_file = obs_data_get_bool(s, "truncate_output_file");
	gf->save_only_while_recording = obs_data_get_bool(s, "only_while_recording");
	gf->rename_file_to_match_recording = obs_data_get_bool(s, "rename_file_to_match_recording");
	// Get the current timestamp using the system clock
	gf->start_timestamp_ms = now_ms();
	gf->sentence_number = 1;
	gf->process_while_muted = obs_data_get_bool(s, "process_while_muted");
	gf->min_sub_duration = (int)obs_data_get_int(s, "min_sub_duration");
	gf->last_sub_render_time = 0;
	bool new_buffered_output = obs_data_get_bool(s, "buffered_output");
	if (new_buffered_output != gf->buffered_output) {
		gf->buffered_output = new_buffered_output;
		gf->overlap_ms = gf->buffered_output ? MAX_OVERLAP_SIZE_MSEC
						     : DEFAULT_OVERLAP_SIZE_MSEC;
		gf->overlap_frames =
			(size_t)((float)gf->sample_rate / (1000.0f / (float)gf->overlap_ms));
	}

	bool new_translate = obs_data_get_bool(s, "translate");
	gf->source_lang = obs_data_get_string(s, "translate_source_language");
	gf->target_lang = obs_data_get_string(s, "translate_target_language");
	gf->translation_ctx.add_context = obs_data_get_bool(s, "translate_add_context");
	gf->translation_output = obs_data_get_string(s, "translate_output");
	gf->suppress_sentences = obs_data_get_string(s, "suppress_sentences");
	gf->translation_model_index = obs_data_get_string(s, "translate_model");

	if (new_translate != gf->translate) {
		if (new_translate) {
			if (gf->translation_model_index != "whisper-based-translation") {
				start_translation(gf);
			} else {
				// whisper-based translation
				obs_log(gf->log_level, "Starting whisper-based translation...");
				gf->translate = false;
			}
		} else {
			gf->translate = false;
		}
	}

	obs_log(gf->log_level, "update text source");
	// update the text source
	const char *new_text_source_name = obs_data_get_string(s, "subtitle_sources");
	obs_weak_source_t *old_weak_text_source = NULL;

	if (new_text_source_name == nullptr || strcmp(new_text_source_name, "none") == 0 ||
	    strcmp(new_text_source_name, "(null)") == 0 ||
	    strcmp(new_text_source_name, "text_file") == 0 || strlen(new_text_source_name) == 0) {
		// new selected text source is not valid, release the old one
		gf->text_source_name.clear();
		gf->output_file_path = "";
		if (strcmp(new_text_source_name, "text_file") == 0) {
			// set the output file path
			const char *output_file_path =
				obs_data_get_string(s, "subtitle_output_filename");
			if (output_file_path != nullptr && strlen(output_file_path) > 0) {
				gf->output_file_path = output_file_path;
			}
		}
	} else {
		// new selected text source is valid, check if it's different from the old one
		if (gf->text_source_name != new_text_source_name) {
			// new text source is different from the old one, release the old one
			gf->text_source_name = new_text_source_name;
		}
	}

	if (old_weak_text_source) {
		obs_log(gf->log_level, "releasing old text source");
		obs_weak_source_release(old_weak_text_source);
	}

	obs_log(gf->log_level, "update whisper model");
	update_whisper_model(gf, s);

	obs_log(gf->log_level, "update whisper params");
	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);

	gf->whisper_params = whisper_full_default_params(
		(whisper_sampling_strategy)obs_data_get_int(s, "whisper_sampling_method"));
	gf->whisper_params.duration_ms = (int)obs_data_get_int(s, "buffer_size_msec");
	if (!new_translate || gf->translation_model_index != "whisper-based-translation") {
		gf->whisper_params.language = obs_data_get_string(s, "whisper_language_select");
	} else {
		// take the language from gf->target_lang
		gf->whisper_params.language = language_codes_2_reverse[gf->target_lang].c_str();
	}
	gf->whisper_params.initial_prompt = obs_data_get_string(s, "initial_prompt");
	gf->whisper_params.n_threads = (int)obs_data_get_int(s, "n_threads");
	gf->whisper_params.n_max_text_ctx = (int)obs_data_get_int(s, "n_max_text_ctx");
	gf->whisper_params.translate = obs_data_get_bool(s, "whisper_translate");
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
	obs_log(LOG_INFO, "LocalVocal filter create");

	void *data = bmalloc(sizeof(struct transcription_filter_data));
	struct transcription_filter_data *gf = new (data) transcription_filter_data();

	// Get the number of channels for the input source
	gf->channels = audio_output_get_channels(obs_get_audio());
	gf->sample_rate = audio_output_get_sample_rate(obs_get_audio());
	gf->frames = (size_t)((float)gf->sample_rate / (1000.0f / MAX_MS_WORK_BUFFER));
	gf->last_num_frames = 0;
	bool step_by_step_processing = obs_data_get_bool(settings, "step_by_step_processing");
	gf->step_size_msec = step_by_step_processing
				     ? (int)obs_data_get_int(settings, "step_size_msec")
				     : obs_data_get_int(settings, "buffer_size_msec");
	gf->min_sub_duration = (int)obs_data_get_int(settings, "min_sub_duration");
	gf->last_sub_render_time = 0;
	gf->log_level = (int)obs_data_get_int(settings, "log_level");
	gf->save_srt = obs_data_get_bool(settings, "subtitle_save_srt");
	gf->truncate_output_file = obs_data_get_bool(settings, "truncate_output_file");
	gf->save_only_while_recording = obs_data_get_bool(settings, "only_while_recording");
	gf->rename_file_to_match_recording =
		obs_data_get_bool(settings, "rename_file_to_match_recording");
	gf->process_while_muted = obs_data_get_bool(settings, "process_while_muted");
	gf->buffered_output = obs_data_get_bool(settings, "buffered_output");

	for (size_t i = 0; i < gf->channels; i++) {
		circlebuf_init(&gf->input_buffers[i]);
	}
	circlebuf_init(&gf->info_buffer);
	circlebuf_init(&gf->whisper_buffer);

	// allocate copy buffers
	gf->copy_buffers[0] =
		static_cast<float *>(bzalloc(gf->channels * gf->frames * sizeof(float)));
	for (size_t c = 1; c < gf->channels; c++) { // set the channel pointers
		gf->copy_buffers[c] = gf->copy_buffers[0] + c * gf->frames;
	}

	gf->context = filter;

	gf->overlap_ms = (int)obs_data_get_int(settings, "overlap_size_msec");
	gf->overlap_frames = (size_t)((float)gf->sample_rate / (1000.0f / (float)gf->overlap_ms));
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

	obs_log(gf->log_level, "clear text source data");
	const char *subtitle_sources = obs_data_get_string(settings, "subtitle_sources");
	if (subtitle_sources == nullptr || strcmp(subtitle_sources, "none") == 0 ||
	    strcmp(subtitle_sources, "(null)") == 0 || strlen(subtitle_sources) == 0) {
		obs_log(LOG_INFO, "create text source");
		// check if a source called "LocalVocal Subtitles" exists
		obs_source_t *source = obs_get_source_by_name("LocalVocal Subtitles");
		if (source) {
			// source exists, release it
			obs_source_release(source);
		} else {
			// create a new OBS text source called "LocalVocal Subtitles"
			obs_source_t *scene_as_source = obs_frontend_get_current_scene();
			obs_scene_t *scene = obs_scene_from_source(scene_as_source);
#ifdef _WIN32
			source = obs_source_create("text_gdiplus_v2", "LocalVocal Subtitles",
						   nullptr, nullptr);
#else
			source = obs_source_create("text_ft2_source_v2", "LocalVocal Subtitles",
						   nullptr, nullptr);
#endif
			if (source) {
				// add source to the current scene
				obs_scene_add(scene, source);
				// set source settings
				obs_data_t *source_settings = obs_source_get_settings(source);
				obs_data_set_bool(source_settings, "word_wrap", true);
				obs_data_set_int(source_settings, "custom_width", 1760);
				obs_data_t *font_data = obs_data_create();
				obs_data_set_string(font_data, "face", "Arial");
				obs_data_set_string(font_data, "style", "Regular");
				obs_data_set_int(font_data, "size", 72);
				obs_data_set_int(font_data, "flags", 0);
				obs_data_set_obj(source_settings, "font", font_data);
				obs_data_release(font_data);
				obs_source_update(source, source_settings);
				obs_data_release(source_settings);

				// set transform settings
				obs_transform_info transform_info;
				transform_info.pos.x = 962.0;
				transform_info.pos.y = 959.0;
				transform_info.bounds.x = 1769.0;
				transform_info.bounds.y = 145.0;
				transform_info.bounds_type =
					obs_bounds_type::OBS_BOUNDS_SCALE_INNER;
				transform_info.bounds_alignment = OBS_ALIGN_CENTER;
				transform_info.alignment = OBS_ALIGN_CENTER;
				transform_info.scale.x = 1.0;
				transform_info.scale.y = 1.0;
				transform_info.rot = 0.0;
				obs_sceneitem_t *source_sceneitem =
					obs_scene_sceneitem_from_source(scene, source);
				obs_sceneitem_set_info(source_sceneitem, &transform_info);
				obs_sceneitem_release(source_sceneitem);

				obs_source_release(source);
			}
			obs_source_release(scene_as_source);
		}
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

	gf->captions_monitor.initialize(
		gf,
		[gf](const std::string &text) {
			obs_log(LOG_INFO, "Captions: %s", text.c_str());
			if (gf->buffered_output) {
				send_caption_to_source(gf->text_source_name, text, gf);
			}
		},
		30, std::chrono::seconds(10));

	obs_log(gf->log_level, "run update");
	// get the settings updated on the filter data struct
	transcription_filter_update(gf, settings);

	gf->active = true;

	// handle the event OBS_FRONTEND_EVENT_RECORDING_STARTING to reset the srt sentence number
	// to match the subtitles with the recording
	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void *private_data) {
			if (event == OBS_FRONTEND_EVENT_RECORDING_STARTING) {
				struct transcription_filter_data *gf_ =
					static_cast<struct transcription_filter_data *>(
						private_data);
				if (gf_->save_srt && gf_->save_only_while_recording) {
					obs_log(gf_->log_level,
						"Recording started. Resetting srt file.");
					// truncate file if it exists
					std::ofstream output_file(gf_->output_file_path,
								  std::ios::out | std::ios::trunc);
					output_file.close();
					gf_->sentence_number = 1;
					gf_->start_timestamp_ms = now_ms();
				}
			} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
				struct transcription_filter_data *gf_ =
					static_cast<struct transcription_filter_data *>(
						private_data);
				if (gf_->save_srt && gf_->save_only_while_recording &&
				    gf_->rename_file_to_match_recording) {
					obs_log(gf_->log_level,
						"Recording stopped. Rename srt file.");
					// rename file to match the recording file name with .srt extension
					// use obs_frontend_get_last_recording to get the last recording file name
					std::string recording_file_name =
						obs_frontend_get_last_recording();
					// remove the extension
					recording_file_name = recording_file_name.substr(
						0, recording_file_name.find_last_of("."));
					std::string srt_file_name = recording_file_name + ".srt";
					// rename the file
					std::rename(gf_->output_file_path.c_str(),
						    srt_file_name.c_str());
				}
			}
		},
		gf);

	obs_log(gf->log_level, "filter created.");
	return gf;
}

bool subs_output_select_changed(obs_properties_t *props, obs_property_t *property,
				obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	// Show or hide the output filename selection input
	const char *new_output = obs_data_get_string(settings, "subtitle_sources");
	const bool show_hide = (strcmp(new_output, "text_file") == 0);
	for (const std::string &prop_name :
	     {"subtitle_output_filename", "subtitle_save_srt", "truncate_output_file",
	      "only_while_recording", "rename_file_to_match_recording"}) {
		obs_property_set_visible(obs_properties_get(props, prop_name.c_str()), show_hide);
	}
	return true;
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

void transcription_filter_defaults(obs_data_t *s)
{
	obs_log(LOG_INFO, "filter defaults");

	obs_data_set_default_bool(s, "buffered_output", false);
	obs_data_set_default_bool(s, "vad_enabled", true);
	obs_data_set_default_int(s, "log_level", LOG_DEBUG);
	obs_data_set_default_bool(s, "log_words", false);
	obs_data_set_default_bool(s, "caption_to_stream", false);
	obs_data_set_default_string(s, "whisper_model_path", "Whisper Tiny English (74Mb)");
	obs_data_set_default_string(s, "whisper_language_select", "en");
	obs_data_set_default_string(s, "subtitle_sources", "none");
	obs_data_set_default_bool(s, "step_by_step_processing", false);
	obs_data_set_default_bool(s, "process_while_muted", false);
	obs_data_set_default_bool(s, "subtitle_save_srt", false);
	obs_data_set_default_bool(s, "truncate_output_file", false);
	obs_data_set_default_bool(s, "only_while_recording", false);
	obs_data_set_default_bool(s, "rename_file_to_match_recording", true);
	obs_data_set_default_int(s, "buffer_size_msec", DEFAULT_BUFFER_SIZE_MSEC);
	obs_data_set_default_int(s, "overlap_size_msec", DEFAULT_OVERLAP_SIZE_MSEC);
	obs_data_set_default_int(s, "step_size_msec", 1000);
	obs_data_set_default_int(s, "min_sub_duration", 3000);
	obs_data_set_default_bool(s, "advanced_settings", false);
	obs_data_set_default_bool(s, "translate", false);
	obs_data_set_default_string(s, "translate_target_language", "__es__");
	obs_data_set_default_string(s, "translate_source_language", "__en__");
	obs_data_set_default_bool(s, "translate_add_context", true);
	obs_data_set_default_string(s, "translate_model", "whisper-based-translation");
	obs_data_set_default_string(s, "suppress_sentences", SUPPRESS_SENTENCES_DEFAULT);

	// Whisper parameters
	obs_data_set_default_int(s, "whisper_sampling_method", WHISPER_SAMPLING_BEAM_SEARCH);
	obs_data_set_default_string(s, "initial_prompt", "");
	obs_data_set_default_int(s, "n_threads", 4);
	obs_data_set_default_int(s, "n_max_text_ctx", 16384);
	obs_data_set_default_bool(s, "whisper_translate", false);
	obs_data_set_default_bool(s, "no_context", false);
	obs_data_set_default_bool(s, "single_segment", true);
	obs_data_set_default_bool(s, "print_special", false);
	obs_data_set_default_bool(s, "print_progress", false);
	obs_data_set_default_bool(s, "print_realtime", false);
	obs_data_set_default_bool(s, "print_timestamps", false);
	obs_data_set_default_bool(s, "token_timestamps", false);
	obs_data_set_default_bool(s, "dtw_token_timestamps", false);
	obs_data_set_default_double(s, "thold_pt", 0.01);
	obs_data_set_default_double(s, "thold_ptsum", 0.01);
	obs_data_set_default_int(s, "max_len", 0);
	obs_data_set_default_bool(s, "split_on_word", true);
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
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	obs_properties_t *ppts = obs_properties_create();

	obs_property_t *subs_output =
		obs_properties_add_list(ppts, "subtitle_sources", MT_("subtitle_sources"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// Add "none" option
	obs_property_list_add_string(subs_output, MT_("none_no_output"), "none");
	obs_property_list_add_string(subs_output, MT_("text_file_output"), "text_file");
	// Add text sources
	obs_enum_sources(add_sources_to_list, subs_output);

	obs_properties_add_path(ppts, "subtitle_output_filename", MT_("output_filename"),
				OBS_PATH_FILE_SAVE, "Text (*.txt)", NULL);
	obs_properties_add_bool(ppts, "subtitle_save_srt", MT_("save_srt"));
	obs_properties_add_bool(ppts, "truncate_output_file", MT_("truncate_output_file"));
	obs_properties_add_bool(ppts, "only_while_recording", MT_("only_while_recording"));
	obs_properties_add_bool(ppts, "rename_file_to_match_recording",
				MT_("rename_file_to_match_recording"));

	obs_property_set_modified_callback(subs_output, subs_output_select_changed);

	// Add a list of available whisper models to download
	obs_property_t *whisper_models_list =
		obs_properties_add_list(ppts, "whisper_model_path", MT_("whisper_model"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// Add models from models_info map
	for (const auto &model_info : models_info) {
		if (model_info.second.type == MODEL_TYPE_TRANSCRIPTION) {
			obs_property_list_add_string(whisper_models_list, model_info.first.c_str(),
						     model_info.first.c_str());
		}
	}

	obs_property_list_add_string(whisper_models_list, "Load external model file",
				     "!!!external!!!");

	// Add a file selection input to select an external model file
	obs_property_t *whisper_model_path_external = obs_properties_add_path(
		ppts, "whisper_model_path_external", MT_("external_model_file"), OBS_PATH_FILE,
		"Model (*.bin)", NULL);
	// Hide the external model file selection input
	obs_property_set_visible(obs_properties_get(ppts, "whisper_model_path_external"), false);

	obs_property_set_modified_callback2(
		whisper_model_path_external,
		[](void *data_, obs_properties_t *props, obs_property_t *property,
		   obs_data_t *settings) {
			obs_log(LOG_INFO, "whisper_model_path_external modified");
			UNUSED_PARAMETER(property);
			UNUSED_PARAMETER(props);
			struct transcription_filter_data *gf_ =
				static_cast<struct transcription_filter_data *>(data_);
			transcription_filter_update(gf_, settings);
			return true;
		},
		gf);

	// Add a callback to the model list to handle the external model file selection
	obs_property_set_modified_callback(whisper_models_list, [](obs_properties_t *props,
								   obs_property_t *property,
								   obs_data_t *settings) {
		UNUSED_PARAMETER(property);
		// If the selected model is the external model, show the external model file selection
		// input
		const char *new_model_path = obs_data_get_string(settings, "whisper_model_path");
		if (strcmp(new_model_path, "!!!external!!!") == 0) {
			obs_property_set_visible(
				obs_properties_get(props, "whisper_model_path_external"), true);
		} else {
			obs_property_set_visible(
				obs_properties_get(props, "whisper_model_path_external"), false);
		}
		return true;
	});

	// add translation option group
	obs_properties_t *translation_group = obs_properties_create();
	obs_property_t *translation_group_prop = obs_properties_add_group(
		ppts, "translate", MT_("translate"), OBS_GROUP_CHECKABLE, translation_group);

	// add translatio model selection
	obs_property_t *prop_translate_model = obs_properties_add_list(
		translation_group, "translate_model", MT_("translate_model"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	// Populate the dropdown with the translation models
	// add "Whisper-Based Translation" option
	obs_property_list_add_string(prop_translate_model, MT_("Whisper-Based-Translation"),
				     "whisper-based-translation");
	for (const auto &model_info : models_info) {
		if (model_info.second.type == MODEL_TYPE_TRANSLATION) {
			obs_property_list_add_string(prop_translate_model, model_info.first.c_str(),
						     model_info.first.c_str());
		}
	}
	// add target language selection
	obs_property_t *prop_tgt = obs_properties_add_list(
		translation_group, "translate_target_language", MT_("target_language"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_t *prop_src = obs_properties_add_list(
		translation_group, "translate_source_language", MT_("source_language"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_properties_add_bool(translation_group, "translate_add_context",
				MT_("translate_add_context"));

	// Populate the dropdown with the language codes
	for (const auto &language : language_codes) {
		obs_property_list_add_string(prop_tgt, language.second.c_str(),
					     language.first.c_str());
		obs_property_list_add_string(prop_src, language.second.c_str(),
					     language.first.c_str());
	}
	// add option for routing the translation to an output source
	obs_property_t *prop_output = obs_properties_add_list(translation_group, "translate_output",
							      MT_("translate_output"),
							      OBS_COMBO_TYPE_LIST,
							      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(prop_output, "Write to captions output", "none");
	// TODO add file output option
	// obs_property_list_add_string(...
	obs_enum_sources(add_sources_to_list, prop_output);

	// add callback to enable/disable translation group
	obs_property_set_modified_callback(translation_group_prop, [](obs_properties_t *props,
								      obs_property_t *property,
								      obs_data_t *settings) {
		UNUSED_PARAMETER(property);
		// Show/Hide the translation group
		const bool translate_enabled = obs_data_get_bool(settings, "translate");
		for (const auto &prop :
		     {"translate_target_language", "translate_source_language",
		      "translate_add_context", "translate_output", "translate_model"}) {
			obs_property_set_visible(obs_properties_get(props, prop),
						 translate_enabled);
		}
		return true;
	});

	obs_property_t *advanced_settings_prop =
		obs_properties_add_bool(ppts, "advanced_settings", MT_("advanced_settings"));
	obs_property_set_modified_callback(advanced_settings_prop, [](obs_properties_t *props,
								      obs_property_t *property,
								      obs_data_t *settings) {
		UNUSED_PARAMETER(property);
		// If advanced settings is enabled, show the advanced settings group
		const bool show_hide = obs_data_get_bool(settings, "advanced_settings");
		for (const std::string &prop_name :
		     {"whisper_params_group", "log_words", "caption_to_stream", "buffer_size_msec",
		      "overlap_size_msec", "step_by_step_processing", "min_sub_duration",
		      "process_while_muted", "buffered_output", "vad_enabled", "log_level",
		      "suppress_sentences"}) {
			obs_property_set_visible(obs_properties_get(props, prop_name.c_str()),
						 show_hide);
		}
		return true;
	});

	obs_property_t *buffered_output_prop =
		obs_properties_add_bool(ppts, "buffered_output", MT_("buffered_output"));
	// add on-change handler for buffered_output
	obs_property_set_modified_callback(buffered_output_prop, [](obs_properties_t *props,
								    obs_property_t *property,
								    obs_data_t *settings) {
		UNUSED_PARAMETER(property);
		UNUSED_PARAMETER(props);
		// if buffered output is enabled set the overlap to max else set it to default
		obs_data_set_int(settings, "overlap_size_msec",
				 obs_data_get_bool(settings, "buffered_output")
					 ? MAX_OVERLAP_SIZE_MSEC
					 : DEFAULT_OVERLAP_SIZE_MSEC);
		return true;
	});

	obs_properties_add_bool(ppts, "log_words", MT_("log_words"));
	obs_properties_add_bool(ppts, "caption_to_stream", MT_("caption_to_stream"));

	obs_properties_add_int_slider(ppts, "buffer_size_msec", MT_("buffer_size_msec"), 1000,
				      DEFAULT_BUFFER_SIZE_MSEC, 250);
	obs_properties_add_int_slider(ppts, "overlap_size_msec", MT_("overlap_size_msec"),
				      MIN_OVERLAP_SIZE_MSEC, MAX_OVERLAP_SIZE_MSEC,
				      (MAX_OVERLAP_SIZE_MSEC - MIN_OVERLAP_SIZE_MSEC) / 5);

	obs_property_t *step_by_step_processing = obs_properties_add_bool(
		ppts, "step_by_step_processing", MT_("step_by_step_processing"));
	obs_properties_add_int_slider(ppts, "step_size_msec", MT_("step_size_msec"), 1000,
				      DEFAULT_BUFFER_SIZE_MSEC, 50);
	obs_properties_add_int_slider(ppts, "min_sub_duration", MT_("min_sub_duration"), 1000, 5000,
				      50);

	obs_property_set_modified_callback(step_by_step_processing, [](obs_properties_t *props,
								       obs_property_t *property,
								       obs_data_t *settings) {
		UNUSED_PARAMETER(property);
		// Show/Hide the step size input
		obs_property_set_visible(obs_properties_get(props, "step_size_msec"),
					 obs_data_get_bool(settings, "step_by_step_processing"));
		return true;
	});

	obs_properties_add_bool(ppts, "process_while_muted", MT_("process_while_muted"));

	obs_properties_add_bool(ppts, "vad_enabled", MT_("vad_enabled"));

	obs_property_t *list = obs_properties_add_list(ppts, "log_level", MT_("log_level"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, "DEBUG (Won't show)", LOG_DEBUG);
	obs_property_list_add_int(list, "INFO", LOG_INFO);
	obs_property_list_add_int(list, "WARNING", LOG_WARNING);

	// add a text input for sentences to suppress
	obs_properties_add_text(ppts, "suppress_sentences", MT_("suppress_sentences"),
				OBS_TEXT_MULTILINE);

	obs_properties_t *whisper_params_group = obs_properties_create();
	obs_properties_add_group(ppts, "whisper_params_group", MT_("whisper_parameters"),
				 OBS_GROUP_NORMAL, whisper_params_group);

	// Add language selector
	obs_property_t *whisper_language_select_list = obs_properties_add_list(
		whisper_params_group, "whisper_language_select", MT_("language"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// sort the languages by flipping the map
	std::map<std::string, std::string> whisper_available_lang_flip;
	for (auto const &pair : whisper_available_lang) {
		whisper_available_lang_flip[pair.second] = pair.first;
	}
	// iterate over all available languages and add them to the list
	for (auto const &pair : whisper_available_lang_flip) {
		// Capitalize the language name
		std::string language_name = pair.first;
		language_name[0] = (char)toupper(language_name[0]);

		obs_property_list_add_string(whisper_language_select_list, language_name.c_str(),
					     pair.second.c_str());
	}

	obs_property_t *whisper_sampling_method_list = obs_properties_add_list(
		whisper_params_group, "whisper_sampling_method", MT_("whisper_sampling_method"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(whisper_sampling_method_list, "Beam search",
				  WHISPER_SAMPLING_BEAM_SEARCH);
	obs_property_list_add_int(whisper_sampling_method_list, "Greedy", WHISPER_SAMPLING_GREEDY);

	// int n_threads;
	obs_properties_add_int_slider(whisper_params_group, "n_threads", MT_("n_threads"), 1, 8, 1);
	// int n_max_text_ctx;     // max tokens to use from past text as prompt for the decoder
	obs_properties_add_int_slider(whisper_params_group, "n_max_text_ctx", MT_("n_max_text_ctx"),
				      0, 16384, 100);
	// int offset_ms;          // start offset in ms
	// int duration_ms;        // audio duration to process in ms
	// bool translate;
	obs_properties_add_bool(whisper_params_group, "whisper_translate",
				MT_("whisper_translate"));
	// bool no_context;        // do not use past transcription (if any) as initial prompt for the decoder
	obs_properties_add_bool(whisper_params_group, "no_context", MT_("no_context"));
	// bool single_segment;    // force single segment output (useful for streaming)
	obs_properties_add_bool(whisper_params_group, "single_segment", MT_("single_segment"));
	// bool print_special;     // print special tokens (e.g. <SOT>, <EOT>, <BEG>, etc.)
	obs_properties_add_bool(whisper_params_group, "print_special", MT_("print_special"));
	// bool print_progress;    // print progress information
	obs_properties_add_bool(whisper_params_group, "print_progress", MT_("print_progress"));
	// bool print_realtime;    // print results from within whisper.cpp (avoid it, use callback instead)
	obs_properties_add_bool(whisper_params_group, "print_realtime", MT_("print_realtime"));
	// bool print_timestamps;  // print timestamps for each text segment when printing realtime
	obs_properties_add_bool(whisper_params_group, "print_timestamps", MT_("print_timestamps"));
	// bool  token_timestamps; // enable token-level timestamps
	obs_properties_add_bool(whisper_params_group, "token_timestamps", MT_("token_timestamps"));
	// enable DTW timestamps
	obs_properties_add_bool(whisper_params_group, "dtw_token_timestamps",
				MT_("dtw_token_timestamps"));
	// float thold_pt;         // timestamp token probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_pt", MT_("thold_pt"), 0.0f,
					1.0f, 0.05f);
	// float thold_ptsum;      // timestamp token sum probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_ptsum", MT_("thold_ptsum"),
					0.0f, 1.0f, 0.05f);
	// int   max_len;          // max segment length in characters
	obs_properties_add_int_slider(whisper_params_group, "max_len", MT_("max_len"), 0, 100, 1);
	// bool  split_on_word;    // split on word rather than on token (when used with max_len)
	obs_properties_add_bool(whisper_params_group, "split_on_word", MT_("split_on_word"));
	// int   max_tokens;       // max tokens per segment (0 = no limit)
	obs_properties_add_int_slider(whisper_params_group, "max_tokens", MT_("max_tokens"), 0, 100,
				      1);
	// bool speed_up;          // speed-up the audio by 2x using Phase Vocoder
	obs_properties_add_bool(whisper_params_group, "speed_up", MT_("speed_up"));
	// const char * initial_prompt;
	obs_properties_add_text(whisper_params_group, "initial_prompt", MT_("initial_prompt"),
				OBS_TEXT_DEFAULT);
	// bool suppress_blank
	obs_properties_add_bool(whisper_params_group, "suppress_blank", MT_("suppress_blank"));
	// bool suppress_non_speech_tokens
	obs_properties_add_bool(whisper_params_group, "suppress_non_speech_tokens",
				MT_("suppress_non_speech_tokens"));
	// float temperature
	obs_properties_add_float_slider(whisper_params_group, "temperature", MT_("temperature"),
					0.0f, 1.0f, 0.05f);
	// float max_initial_ts
	obs_properties_add_float_slider(whisper_params_group, "max_initial_ts",
					MT_("max_initial_ts"), 0.0f, 1.0f, 0.05f);
	// float length_penalty
	obs_properties_add_float_slider(whisper_params_group, "length_penalty",
					MT_("length_penalty"), -1.0f, 1.0f, 0.1f);

	// Add a informative text about the plugin
	obs_properties_add_text(
		ppts, "info",
		QString(PLUGIN_INFO_TEMPLATE).arg(PLUGIN_VERSION).toStdString().c_str(),
		OBS_TEXT_INFO);

	UNUSED_PARAMETER(data);
	return ppts;
}
