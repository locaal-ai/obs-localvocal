#ifdef _WIN32
#define NOMINMAX
#endif

#include <obs.h>
#include <obs-frontend-api.h>

#include <curl/curl.h>

#include <fstream>
#include <iomanip>
#include <regex>
#include <string>
#include <vector>

#include "transcription-filter-callbacks.h"
#include "transcription-utils.h"
#include "translation/translation.h"
#include "translation/translation-includes.h"

#define SEND_TIMED_METADATA_URL "http://localhost:8080/timed-metadata"

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

void send_timed_metadata(struct transcription_filter_data *gf,
			 const DetectionResultWithText &result)
{
	std::thread send_timed_metadata_thread([result]() {
		// create a CURL HTTP POST request with the result in the body
		CURL *curl = curl_easy_init();
		if (curl) {
			// create the JSON body
			std::string json_body = "{\"start_timestamp_ms\": " +
						std::to_string(result.start_timestamp_ms) +
						", \"end_timestamp_ms\": " +
						std::to_string(result.end_timestamp_ms) +
						", \"text\": \"" + result.text + "\"}";
			// set the URL
			curl_easy_setopt(curl, CURLOPT_URL, SEND_TIMED_METADATA_URL);
			// set the headers
			struct curl_slist *headers = NULL;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			// set the body
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
			// perform the request
			CURLcode res = curl_easy_perform(curl);
			if (res != CURLE_OK) {
				obs_log(LOG_ERROR, "Failed to send timed metadata: %s",
					curl_easy_strerror(res));
			}
			// cleanup
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
		}
	});

	send_timed_metadata_thread.detach();
}

void audio_chunk_callback(struct transcription_filter_data *gf, const float *pcm32f_data,
			  size_t frames, int vad_state, const DetectionResultWithText &result)
{
	UNUSED_PARAMETER(gf);
	UNUSED_PARAMETER(pcm32f_data);
	UNUSED_PARAMETER(frames);
	UNUSED_PARAMETER(vad_state);
	UNUSED_PARAMETER(result);
	// stub
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
		// only remove leading and trailing non-alphanumeric characters if the output is English
		str_copy = remove_leading_trailing_nonalpha(str_copy);
	}

	// if suppression is enabled, check if the text is in the suppression list
	if (!gf->suppress_sentences.empty()) {
		// split the suppression list by newline into individual sentences
		std::vector<std::string> suppress_sentences_list =
			split(gf->suppress_sentences, '\n');
		const std::string original_str_copy = str_copy;
		// check if the text is in the suppression list
		for (const std::string &suppress_sentence : suppress_sentences_list) {
			// if suppress_sentence exists within str_copy, remove it (replace with "")
			str_copy = std::regex_replace(str_copy, std::regex(suppress_sentence), "");
		}
		// if the text was modified, log the original and modified text
		if (original_str_copy != str_copy) {
			obs_log(gf->log_level, "------ Suppressed text: '%s' -> '%s'",
				original_str_copy.c_str(), str_copy.c_str());
		}
		if (remove_leading_trailing_nonalpha(str_copy).empty()) {
			// if the text is empty after suppression, return
			return;
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
				if (gf->buffered_output) {
					gf->captions_monitor.addSentence(translated_text);
				} else {
					// send the translation to the selected source
					send_caption_to_source(gf->translation_output,
							       translated_text, gf);
				}
			}
		} else {
			obs_log(gf->log_level, "Failed to translate text");
		}
	}

	gf->last_text = str_copy;

	if (gf->buffered_output) {
		gf->captions_monitor.addSentence(str_copy);
	}

	if (gf->caption_to_stream) {
		obs_output_t *streaming_output = obs_frontend_get_streaming_output();
		if (streaming_output) {
			obs_output_output_caption_text1(streaming_output, str_copy.c_str());
			obs_output_release(streaming_output);
		}
	}

	if (gf->send_timed_metadata) {
		send_timed_metadata(gf, result);
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
