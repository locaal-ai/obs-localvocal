#include "whisper-utils.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"
#include "whisper-processing.h"
#include "vad-processing.h"

#include <obs-module.h>

void shutdown_whisper_thread(struct transcription_filter_data *gf)
{
	obs_log(gf->log_level, "shutdown_whisper_thread");
	if (gf->whisper_context != nullptr) {
		// acquire the mutex before freeing the context
		std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
		whisper_free(gf->whisper_context);
		gf->whisper_context = nullptr;
		gf->wshiper_thread_cv.notify_all();
	}
	if (gf->whisper_thread.joinable()) {
		gf->whisper_thread.join();
	}
	if (!gf->whisper_model_path.empty()) {
		gf->whisper_model_path = "";
	}
}

void start_whisper_thread_with_path(struct transcription_filter_data *gf,
				    const std::string &whisper_model_path,
				    const char *silero_vad_model_file)
{
	obs_log(gf->log_level, "start_whisper_thread_with_path: %s, silero model path: %s",
		whisper_model_path.c_str(), silero_vad_model_file);
	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
	if (gf->whisper_context != nullptr) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_context is not null");
		return;
	}

	// initialize Silero VAD
	initialize_vad(gf, silero_vad_model_file);

	obs_log(gf->log_level, "Create whisper context");
	gf->whisper_context = init_whisper_context(whisper_model_path, gf);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_ERROR, "Failed to initialize whisper context");
		return;
	}
	gf->whisper_model_file_currently_loaded = whisper_model_path;
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
	for (size_t i = seq1.size() - 2; i >= seq1.size() / 2; --i) {
		for (size_t j = 0; j < seq2.size() - 1; ++j) {
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
		if (seq1.empty() && seq2.empty()) {
			return reconstructed;
		}
		if (seq1.empty()) {
			return seq2;
		}
		if (seq2.empty()) {
			return seq1;
		}

		// Return concat of seq1 and seq2 if no overlap found
		// check if the last token of seq1 == the first token of seq2
		if (seq1.back().id == seq2.front().id) {
			// don't add the last token of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end() - 1);
			reconstructed.insert(reconstructed.end(), seq2.begin(), seq2.end());
		} else if (seq2.size() > 1ull && seq1.back().id == seq2[1].id) {
			// check if the last token of seq1 == the second token of seq2
			// don't add the last token of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end() - 1);
			// don't add the first token of seq2
			reconstructed.insert(reconstructed.end(), seq2.begin() + 1, seq2.end());
		} else if (seq1.size() > 1ull && seq1[seq1.size() - 2].id == seq2.front().id) {
			// check if the second to last token of seq1 == the first token of seq2
			// don't add the last two tokens of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end() - 2);
			reconstructed.insert(reconstructed.end(), seq2.begin(), seq2.end());
		} else {
			// add all tokens of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end());
			reconstructed.insert(reconstructed.end(), seq2.begin(), seq2.end());
		}
		return reconstructed;
	}

	// Add tokens from the first sequence up to the overlap
	reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.begin() + overlap.first);

	// Determine the length of the overlap
	size_t overlapLength = 0;
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

std::string to_timestamp(uint64_t t_ms_offset)
{
	uint64_t sec = t_ms_offset / 1000;
	uint64_t msec = t_ms_offset - sec * 1000;
	uint64_t min = sec / 60;
	sec = sec - min * 60;

	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%02d.%03d", (int)min, (int)sec, (int)msec);

	return std::string(buf);
}

void whisper_params_pretty_print(whisper_full_params &params)
{
    obs_log(LOG_INFO, "Whisper params:");
    obs_log(LOG_INFO, "strategy: %s", params.strategy == whisper_sampling_strategy::WHISPER_SAMPLING_BEAM_SEARCH ? "beam_search" : "greedy");
    obs_log(LOG_INFO, "n_threads: %d", params.n_threads);
    obs_log(LOG_INFO, "n_max_text_ctx: %d", params.n_max_text_ctx);
    obs_log(LOG_INFO, "offset_ms: %d", params.offset_ms);
    obs_log(LOG_INFO, "duration_ms: %d", params.duration_ms);
    obs_log(LOG_INFO, "translate: %s", params.translate ? "true" : "false");
    obs_log(LOG_INFO, "no_context: %s", params.no_context ? "true" : "false");
    obs_log(LOG_INFO, "no_timestamps: %s", params.no_timestamps ? "true" : "false");
    obs_log(LOG_INFO, "single_segment: %s", params.single_segment ? "true" : "false");
    obs_log(LOG_INFO, "print_special: %s", params.print_special ? "true" : "false");
    obs_log(LOG_INFO, "print_progress: %s", params.print_progress ? "true" : "false");
    obs_log(LOG_INFO, "print_realtime: %s", params.print_realtime ? "true" : "false");
    obs_log(LOG_INFO, "print_timestamps: %s", params.print_timestamps ? "true" : "false");
    obs_log(LOG_INFO, "token_timestamps: %s", params.token_timestamps ? "true" : "false");
    obs_log(LOG_INFO, "thold_pt: %f", params.thold_pt);
    obs_log(LOG_INFO, "thold_ptsum: %f", params.thold_ptsum);
    obs_log(LOG_INFO, "max_len: %d", params.max_len);
    obs_log(LOG_INFO, "split_on_word: %s", params.split_on_word ? "true" : "false");
    obs_log(LOG_INFO, "max_tokens: %d", params.max_tokens);
    obs_log(LOG_INFO, "debug_mode: %s", params.debug_mode ? "true" : "false");
    obs_log(LOG_INFO, "audio_ctx: %d", params.audio_ctx);
    obs_log(LOG_INFO, "tdrz_enable: %s", params.tdrz_enable ? "true" : "false");
    obs_log(LOG_INFO, "suppress_regex: %s", params.suppress_regex);
    obs_log(LOG_INFO, "initial_prompt: %s", params.initial_prompt);
    obs_log(LOG_INFO, "language: %s", params.language);
    obs_log(LOG_INFO, "detect_language: %s", params.detect_language ? "true" : "false");
    obs_log(LOG_INFO, "suppress_blank: %s", params.suppress_blank ? "true" : "false");
    obs_log(LOG_INFO, "suppress_non_speech_tokens: %s", params.suppress_non_speech_tokens ? "true" : "false");
    obs_log(LOG_INFO, "temperature: %f", params.temperature);
    obs_log(LOG_INFO, "max_initial_ts: %f", params.max_initial_ts);
    obs_log(LOG_INFO, "length_penalty: %f", params.length_penalty);
    obs_log(LOG_INFO, "temperature_inc: %f", params.temperature_inc);
    obs_log(LOG_INFO, "entropy_thold: %f", params.entropy_thold);
    obs_log(LOG_INFO, "logprob_thold: %f", params.logprob_thold);
    obs_log(LOG_INFO, "no_speech_thold: %f", params.no_speech_thold);
    obs_log(LOG_INFO, "greedy.best_of: %d", params.greedy.best_of);
    obs_log(LOG_INFO, "beam_search.beam_size: %d", params.beam_search.beam_size);
    obs_log(LOG_INFO, "beam_search.patience: %f", params.beam_search.patience);
}

void apply_whisper_params_defaults_on_settings(obs_data_t *s) {
    whisper_full_params whisper_params_tmp = whisper_full_default_params(whisper_sampling_strategy::WHISPER_SAMPLING_BEAM_SEARCH);

    obs_data_set_default_int(s, "strategy", whisper_sampling_strategy::WHISPER_SAMPLING_BEAM_SEARCH);
    obs_data_set_default_int(s, "n_threads", whisper_params_tmp.n_threads);
    obs_data_set_default_int(s, "n_max_text_ctx", whisper_params_tmp.n_max_text_ctx);
    obs_data_set_default_int(s, "offset_ms", whisper_params_tmp.offset_ms);
    obs_data_set_default_int(s, "duration_ms", whisper_params_tmp.duration_ms);
    obs_data_set_default_bool(s, "whisper_translate", whisper_params_tmp.translate);
    obs_data_set_default_bool(s, "no_context", whisper_params_tmp.no_context);
    obs_data_set_default_bool(s, "no_timestamps", whisper_params_tmp.no_timestamps);
    obs_data_set_default_bool(s, "single_segment", whisper_params_tmp.single_segment);
    obs_data_set_default_bool(s, "print_special", whisper_params_tmp.print_special);
    obs_data_set_default_bool(s, "print_progress", whisper_params_tmp.print_progress);
    obs_data_set_default_bool(s, "print_realtime", whisper_params_tmp.print_realtime);
    obs_data_set_default_bool(s, "print_timestamps", whisper_params_tmp.print_timestamps);
    obs_data_set_default_bool(s, "token_timestamps", whisper_params_tmp.token_timestamps);
    obs_data_set_default_double(s, "thold_pt", whisper_params_tmp.thold_pt);
    obs_data_set_default_double(s, "thold_ptsum", whisper_params_tmp.thold_ptsum);
    obs_data_set_default_int(s, "max_len", whisper_params_tmp.max_len);
    obs_data_set_default_bool(s, "split_on_word", whisper_params_tmp.split_on_word);
    obs_data_set_default_int(s, "max_tokens", whisper_params_tmp.max_tokens);
    obs_data_set_default_bool(s, "debug_mode", whisper_params_tmp.debug_mode);
    obs_data_set_default_int(s, "audio_ctx", whisper_params_tmp.audio_ctx);
    obs_data_set_default_bool(s, "tdrz_enable", whisper_params_tmp.tdrz_enable);
    obs_data_set_default_string(s, "suppress_regex", whisper_params_tmp.suppress_regex);
    obs_data_set_default_string(s, "initial_prompt", whisper_params_tmp.initial_prompt);
    obs_data_set_default_string(s, "language", whisper_params_tmp.language);
    obs_data_set_default_bool(s, "detect_language", whisper_params_tmp.detect_language);
    obs_data_set_default_bool(s, "suppress_blank", whisper_params_tmp.suppress_blank);
    obs_data_set_default_bool(s, "suppress_non_speech_tokens", whisper_params_tmp.suppress_non_speech_tokens);
    obs_data_set_default_double(s, "temperature", whisper_params_tmp.temperature);
    obs_data_set_default_double(s, "max_initial_ts", whisper_params_tmp.max_initial_ts);
    obs_data_set_default_double(s, "length_penalty", whisper_params_tmp.length_penalty);
    obs_data_set_default_double(s, "temperature_inc", whisper_params_tmp.temperature_inc);
    obs_data_set_default_double(s, "entropy_thold", whisper_params_tmp.entropy_thold);
    obs_data_set_default_double(s, "logprob_thold", whisper_params_tmp.logprob_thold);
    obs_data_set_default_double(s, "no_speech_thold", whisper_params_tmp.no_speech_thold);
    obs_data_set_default_int(s, "greedy.best_of", whisper_params_tmp.greedy.best_of);
    obs_data_set_default_int(s, "beam_search.beam_size", whisper_params_tmp.beam_search.beam_size);
    obs_data_set_default_double(s, "beam_search.patience", whisper_params_tmp.beam_search.patience);
}

void apply_whisper_params_from_settings(whisper_full_params &params, obs_data_t *settings) {
    params = whisper_full_default_params((whisper_sampling_strategy)obs_data_get_int(settings, "strategy"));
    params.n_threads = obs_data_get_int(settings, "n_threads");
    params.n_max_text_ctx = obs_data_get_int(settings, "n_max_text_ctx");
    params.offset_ms = obs_data_get_int(settings, "offset_ms");
    params.duration_ms = obs_data_get_int(settings, "duration_ms");
    params.translate = obs_data_get_bool(settings, "whisper_translate");
    params.no_context = obs_data_get_bool(settings, "no_context");
    params.no_timestamps = obs_data_get_bool(settings, "no_timestamps");
    params.single_segment = obs_data_get_bool(settings, "single_segment");
    params.print_special = obs_data_get_bool(settings, "print_special");
    params.print_progress = obs_data_get_bool(settings, "print_progress");
    params.print_realtime = obs_data_get_bool(settings, "print_realtime");
    params.print_timestamps = obs_data_get_bool(settings, "print_timestamps");
    params.token_timestamps = obs_data_get_bool(settings, "token_timestamps");
    params.thold_pt = obs_data_get_double(settings, "thold_pt");
    params.thold_ptsum = obs_data_get_double(settings, "thold_ptsum");
    params.max_len = obs_data_get_int(settings, "max_len");
    params.split_on_word = obs_data_get_bool(settings, "split_on_word");
    params.max_tokens = obs_data_get_int(settings, "max_tokens");
    params.debug_mode = obs_data_get_bool(settings, "debug_mode");
    params.audio_ctx = obs_data_get_int(settings, "audio_ctx");
    params.tdrz_enable = obs_data_get_bool(settings, "tdrz_enable");
    params.suppress_regex = obs_data_get_string(settings, "suppress_regex");
    params.initial_prompt = obs_data_get_string(settings, "initial_prompt");
    params.language = obs_data_get_string(settings, "language");
    params.detect_language = obs_data_get_bool(settings, "detect_language");
    params.suppress_blank = obs_data_get_bool(settings, "suppress_blank");
    params.suppress_non_speech_tokens = obs_data_get_bool(settings, "suppress_non_speech_tokens");
    params.temperature = obs_data_get_double(settings, "temperature");
    params.max_initial_ts = obs_data_get_double(settings, "max_initial_ts");
    params.length_penalty = obs_data_get_double(settings, "length_penalty");
    params.temperature_inc = obs_data_get_double(settings, "temperature_inc");
    params.entropy_thold = obs_data_get_double(settings, "entropy_thold");
    params.logprob_thold = obs_data_get_double(settings, "logprob_thold");
    params.no_speech_thold = obs_data_get_double(settings, "no_speech_thold");
    params.greedy.best_of = obs_data_get_int(settings, "greedy.best_of");
    params.beam_search.beam_size = obs_data_get_int(settings, "beam_search.beam_size");
    params.beam_search.patience = obs_data_get_double(settings, "beam_search.patience");
}
