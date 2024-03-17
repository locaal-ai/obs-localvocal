#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"
#include "transcription-filter.h"
#include "transcription-filter-data.h"
#include "model-utils/model-downloader.h"
#include "whisper-utils/whisper-processing.h"
#include "whisper-utils/whisper-language.h"
#include "whisper-utils/whisper-utils.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <bitset>
#ifdef _WIN32
#include <Windows.h>
#endif

#include <QString>

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

inline uint64_t now_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		       std::chrono::system_clock::now().time_since_epoch())
		.count();
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

	if (!gf->whisper_buf_mutex || !gf->whisper_ctx_mutex) {
		obs_log(LOG_ERROR, "whisper mutexes are null");
		return audio;
	}

	{
		std::lock_guard<std::mutex> lock(*gf->whisper_buf_mutex); // scoped lock
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

	obs_log(gf->log_level, "transcription_filter_destroy");
	shutdown_whisper_thread(gf);

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

	delete gf->whisper_buf_mutex;
	delete gf->whisper_ctx_mutex;
	delete gf->wshiper_thread_cv;
	delete gf->text_source_mutex;

	delete gf;
}

void acquire_weak_text_source_ref(struct transcription_filter_data *gf)
{
	if (!gf->text_source_name) {
		obs_log(gf->log_level, "text_source_name is null");
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

#define is_lead_byte(c) (((c) & 0xe0) == 0xc0 || ((c) & 0xf0) == 0xe0 || ((c) & 0xf8) == 0xf0)
#define is_trail_byte(c) (((c) & 0xc0) == 0x80)

inline int lead_byte_length(const uint8_t c)
{
	if ((c & 0xe0) == 0xc0) {
		return 2;
	} else if ((c & 0xf0) == 0xe0) {
		return 3;
	} else if ((c & 0xf8) == 0xf0) {
		return 4;
	} else {
		return 1;
	}
}

inline bool is_valid_lead_byte(const uint8_t *c)
{
	const int length = lead_byte_length(c[0]);
	if (length == 1) {
		return true;
	}
	if (length == 2 && is_trail_byte(c[1])) {
		return true;
	}
	if (length == 3 && is_trail_byte(c[1]) && is_trail_byte(c[2])) {
		return true;
	}
	if (length == 4 && is_trail_byte(c[1]) && is_trail_byte(c[2]) && is_trail_byte(c[3])) {
		return true;
	}
	return false;
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

#ifdef _WIN32
	// Some UTF8 charsets on Windows output have a bug, instead of 0xd? it outputs
	// 0xf?, and 0xc? becomes 0xe?, so we need to fix it.
	std::stringstream ss;
	uint8_t *c_str = (uint8_t *)result.text.c_str();
	for (size_t i = 0; i < result.text.size(); ++i) {
		if (is_lead_byte(c_str[i])) {
			// this is a unicode leading byte
			// if the next char is 0xff - it's a bug char, replace it with 0x9f
			if (c_str[i + 1] == 0xff) {
				c_str[i + 1] = 0x9f;
			}
			if (!is_valid_lead_byte(c_str + i)) {
				// This is a bug lead byte, because it's length 3 and the i+2 byte is also
				// a lead byte
				c_str[i] = c_str[i] - 0x20;
			}
		} else {
			if (c_str[i] >= 0xf8) {
				// this may be a malformed lead byte.
				// lets see if it becomes a valid lead byte if we "fix" it
				uint8_t buf_[4];
				buf_[0] = c_str[i] - 0x20;
				buf_[1] = c_str[i + 1];
				buf_[2] = c_str[i + 2];
				buf_[3] = c_str[i + 3];
				if (is_valid_lead_byte(buf_)) {
					// this is a malformed lead byte, fix it
					c_str[i] = c_str[i] - 0x20;
				}
			}
		}
	}

	std::string str_copy = (char *)c_str;
#else
	std::string str_copy = result.text;
#endif

	if (gf->caption_to_stream) {
		obs_output_t *streaming_output = obs_frontend_get_streaming_output();
		if (streaming_output) {
			obs_output_output_caption_text1(streaming_output, str_copy.c_str());
			obs_output_release(streaming_output);
		}
	}

	if (gf->output_file_path != "" && !gf->text_source_name) {
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
		if (!gf->text_source_mutex) {
			obs_log(LOG_ERROR, "text_source_mutex is null");
			return;
		}

		if (!gf->text_source) {
			// attempt to acquire a weak ref to the text source if it's yet available
			acquire_weak_text_source_ref(gf);
		}

		std::lock_guard<std::mutex> lock(*gf->text_source_mutex);

		if (!gf->text_source) {
			obs_log(gf->log_level, "text_source is null");
			return;
		}
		auto target = obs_weak_source_get_source(gf->text_source);
		if (!target) {
			obs_log(gf->log_level, "text_source target is null");
			return;
		}
		auto text_settings = obs_source_get_settings(target);
		obs_data_set_string(text_settings, "text", str_copy.c_str());
		obs_source_update(target, text_settings);
		obs_source_release(target);
	}
};

void transcription_filter_update(void *data, obs_data_t *s)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	gf->log_level = (int)obs_data_get_int(s, "log_level");
	obs_log(gf->log_level, "transcription_filter_update");

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

	obs_log(gf->log_level, "transcription_filter: update text source");
	// update the text source
	const char *new_text_source_name = obs_data_get_string(s, "subtitle_sources");
	obs_weak_source_t *old_weak_text_source = NULL;

	if (new_text_source_name == nullptr || strcmp(new_text_source_name, "none") == 0 ||
	    strcmp(new_text_source_name, "(null)") == 0 ||
	    strcmp(new_text_source_name, "text_file") == 0 || strlen(new_text_source_name) == 0) {
		// new selected text source is not valid, release the old one
		if (gf->text_source) {
			if (!gf->text_source_mutex) {
				obs_log(LOG_ERROR, "text_source_mutex is null");
				return;
			}
			std::lock_guard<std::mutex> lock(*gf->text_source_mutex);
			old_weak_text_source = gf->text_source;
			gf->text_source = nullptr;
		}
		if (gf->text_source_name) {
			bfree(gf->text_source_name);
			gf->text_source_name = nullptr;
		}
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
		if (gf->text_source_name == nullptr ||
		    strcmp(new_text_source_name, gf->text_source_name) != 0) {
			// new text source is different from the old one, release the old one
			if (gf->text_source) {
				if (!gf->text_source_mutex) {
					obs_log(LOG_ERROR, "text_source_mutex is null");
					return;
				}
				std::lock_guard<std::mutex> lock(*gf->text_source_mutex);
				old_weak_text_source = gf->text_source;
				gf->text_source = nullptr;
			}
			if (gf->text_source_name) {
				// free the old text source name
				bfree(gf->text_source_name);
				gf->text_source_name = nullptr;
			}
			gf->text_source_name = bstrdup(new_text_source_name);
		}
	}

	if (old_weak_text_source) {
		obs_log(gf->log_level, "releasing old text source");
		obs_weak_source_release(old_weak_text_source);
	}

	if (gf->whisper_ctx_mutex == nullptr) {
		obs_log(LOG_ERROR, "whisper_ctx_mutex is null");
		return;
	}

	obs_log(gf->log_level, "transcription_filter: update whisper model");
	update_whsiper_model_path(gf, s);

	obs_log(gf->log_level, "transcription_filter: update whisper params");
	std::lock_guard<std::mutex> lock(*gf->whisper_ctx_mutex);

	gf->whisper_params = whisper_full_default_params(
		(whisper_sampling_strategy)obs_data_get_int(s, "whisper_sampling_method"));
	gf->whisper_params.duration_ms = (int)obs_data_get_int(s, "buffer_size_msec");
	gf->whisper_params.language = obs_data_get_string(s, "whisper_language_select");
	gf->whisper_params.initial_prompt = obs_data_get_string(s, "initial_prompt");
	gf->whisper_params.n_threads = (int)obs_data_get_int(s, "n_threads");
	gf->whisper_params.n_max_text_ctx = (int)obs_data_get_int(s, "n_max_text_ctx");
	gf->whisper_params.translate = obs_data_get_bool(s, "translate");
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
	obs_log(LOG_INFO, "transcription filter create");

	struct transcription_filter_data *gf = new transcription_filter_data();

	// Get the number of channels for the input source
	gf->channels = audio_output_get_channels(obs_get_audio());
	gf->sample_rate = audio_output_get_sample_rate(obs_get_audio());
	gf->frames = (size_t)((float)gf->sample_rate /
			      (1000.0f / (float)obs_data_get_int(settings, "buffer_size_msec")));
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

	for (size_t i = 0; i < gf->channels; i++) {
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

	gf->overlap_ms = (int)obs_data_get_int(settings, "overlap_size_msec");
	gf->overlap_frames = (size_t)((float)gf->sample_rate / (1000.0f / (float)gf->overlap_ms));
	obs_log(gf->log_level, "transcription_filter: channels %d, frames %d, sample_rate %d",
		(int)gf->channels, (int)gf->frames, gf->sample_rate);

	obs_log(gf->log_level, "transcription_filter: setup audio resampler");
	struct resample_info src, dst;
	src.samples_per_sec = gf->sample_rate;
	src.format = AUDIO_FORMAT_FLOAT_PLANAR;
	src.speakers = convert_speaker_layout((uint8_t)gf->channels);

	dst.samples_per_sec = WHISPER_SAMPLE_RATE;
	dst.format = AUDIO_FORMAT_FLOAT_PLANAR;
	dst.speakers = convert_speaker_layout((uint8_t)1);

	gf->resampler = audio_resampler_create(&dst, &src);

	obs_log(gf->log_level, "transcription_filter: setup mutexes and condition variables");
	gf->whisper_buf_mutex = new std::mutex();
	gf->whisper_ctx_mutex = new std::mutex();
	gf->wshiper_thread_cv = new std::condition_variable();
	gf->text_source_mutex = new std::mutex();
	obs_log(gf->log_level, "transcription_filter: clear text source data");
	gf->text_source = nullptr;
	const char *subtitle_sources = obs_data_get_string(settings, "subtitle_sources");
	if (subtitle_sources != nullptr) {
		gf->text_source_name = bstrdup(subtitle_sources);
	} else {
		gf->text_source_name = nullptr;
	}
	obs_log(gf->log_level, "transcription_filter: clear paths and whisper context");
	gf->whisper_model_file_currently_loaded = "";
	gf->output_file_path = std::string("");
	gf->whisper_model_path = nullptr; // The update function will set the model path
	gf->whisper_context = nullptr;

	obs_log(gf->log_level, "transcription_filter: run update");
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

	obs_log(gf->log_level, "transcription_filter: filter created.");
	return gf;
}

bool subs_output_select_changed(obs_properties_t *props, obs_property_t *property,
				obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	// Show or hide the output filename selection input
	const char *new_output = obs_data_get_string(settings, "subtitle_sources");
	const bool show_hide = (strcmp(new_output, "text_file") == 0);
	obs_property_set_visible(obs_properties_get(props, "subtitle_output_filename"), show_hide);
	obs_property_set_visible(obs_properties_get(props, "subtitle_save_srt"), show_hide);
	obs_property_set_visible(obs_properties_get(props, "truncate_output_file"), show_hide);
	obs_property_set_visible(obs_properties_get(props, "only_while_recording"), show_hide);
	obs_property_set_visible(obs_properties_get(props, "rename_file_to_match_recording"),
				 show_hide);
	return true;
}

void transcription_filter_activate(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(gf->log_level, "transcription_filter filter activated");
	gf->active = true;
}

void transcription_filter_deactivate(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);
	obs_log(gf->log_level, "transcription_filter filter deactivated");
	gf->active = false;
}

void transcription_filter_defaults(obs_data_t *s)
{
	obs_log(LOG_INFO, "transcription_filter_defaults");

	obs_data_set_default_bool(s, "vad_enabled", true);
	obs_data_set_default_int(s, "log_level", LOG_DEBUG);
	obs_data_set_default_bool(s, "log_words", true);
	obs_data_set_default_bool(s, "caption_to_stream", false);
	obs_data_set_default_string(s, "whisper_model_path", "models/ggml-tiny.en.bin");
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

	// Whisper parameters
	obs_data_set_default_int(s, "whisper_sampling_method", WHISPER_SAMPLING_BEAM_SEARCH);
	obs_data_set_default_string(s, "initial_prompt", "");
	obs_data_set_default_int(s, "n_threads", 4);
	obs_data_set_default_int(s, "n_max_text_ctx", 16384);
	obs_data_set_default_bool(s, "translate", false);
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
	obs_log(LOG_INFO, "transcription_filter_properties");

	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	obs_properties_t *ppts = obs_properties_create();

	obs_properties_add_bool(ppts, "log_words", MT_("log_words"));
	obs_properties_add_bool(ppts, "caption_to_stream", MT_("caption_to_stream"));

	obs_properties_add_int_slider(ppts, "buffer_size_msec", MT_("buffer_size_msec"), 1000,
				      DEFAULT_BUFFER_SIZE_MSEC, 50);
	obs_properties_add_int_slider(ppts, "overlap_size_msec", MT_("overlap_size_msec"), 50, 300,
				      50);

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

	obs_property_list_add_string(whisper_models_list, "Tiny (Eng) 75Mb",
				     "models/ggml-tiny.en.bin");
	obs_property_list_add_string(whisper_models_list, "Tiny 75Mb", "models/ggml-tiny.bin");
	obs_property_list_add_string(whisper_models_list, "Base (Eng) 142Mb",
				     "models/ggml-base.en.bin");
	obs_property_list_add_string(whisper_models_list, "Base 142Mb", "models/ggml-base.bin");
	obs_property_list_add_string(whisper_models_list, "Small (Eng) 466Mb",
				     "models/ggml-small.en.bin");
	obs_property_list_add_string(whisper_models_list, "Small 466Mb", "models/ggml-small.bin");
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

	obs_property_t *advanced_settings_prop =
		obs_properties_add_bool(ppts, "advanced_settings", MT_("advanced_settings"));
	obs_property_set_modified_callback(advanced_settings_prop, [](obs_properties_t *props,
								      obs_property_t *property,
								      obs_data_t *settings) {
		UNUSED_PARAMETER(property);
		// If advanced settings is enabled, show the advanced settings group
		const bool show_hide = obs_data_get_bool(settings, "advanced_settings");
		obs_property_set_visible(obs_properties_get(props, "whisper_params_group"),
					 show_hide);
		return true;
	});

	obs_properties_t *whisper_params_group = obs_properties_create();
	obs_properties_add_group(ppts, "whisper_params_group", MT_("whisper_parameters"),
				 OBS_GROUP_NORMAL, whisper_params_group);

	obs_properties_add_bool(whisper_params_group, "vad_enabled", MT_("vad_enabled"));
	obs_property_t *list = obs_properties_add_list(whisper_params_group, "log_level",
						       MT_("log_level"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, "DEBUG", LOG_DEBUG);
	obs_property_list_add_int(list, "INFO", LOG_INFO);
	obs_property_list_add_int(list, "WARNING", LOG_WARNING);

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
	obs_properties_add_bool(whisper_params_group, "translate", MT_("translate"));
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
