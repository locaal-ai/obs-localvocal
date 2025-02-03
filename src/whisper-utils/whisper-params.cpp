#include "whisper-params.h"

#include <obs-module.h>

#define MT_ obs_module_text

void whisper_params_pretty_print(whisper_full_params &params)
{
	obs_log(LOG_INFO, "Whisper params:");
	obs_log(LOG_INFO, "strategy: %s",
		params.strategy == whisper_sampling_strategy::WHISPER_SAMPLING_BEAM_SEARCH
			? "beam_search"
			: "greedy");
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
	obs_log(LOG_INFO, "suppress_nst: %s", params.suppress_nst ? "true" : "false");
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

void apply_whisper_params_defaults_on_settings(obs_data_t *s)
{
	whisper_full_params whisper_params_tmp = whisper_full_default_params(
		whisper_sampling_strategy::WHISPER_SAMPLING_BEAM_SEARCH);

	obs_data_set_default_int(s, "strategy",
				 whisper_sampling_strategy::WHISPER_SAMPLING_BEAM_SEARCH);
	obs_data_set_default_int(s, "n_threads", whisper_params_tmp.n_threads);
	obs_data_set_default_int(s, "n_max_text_ctx", whisper_params_tmp.n_max_text_ctx);
	obs_data_set_default_int(s, "offset_ms", whisper_params_tmp.offset_ms);
	obs_data_set_default_int(s, "duration_ms", whisper_params_tmp.duration_ms);
	obs_data_set_default_bool(s, "whisper_translate", whisper_params_tmp.translate);
	obs_data_set_default_bool(s, "no_context", whisper_params_tmp.no_context);
	obs_data_set_default_bool(s, "no_timestamps", whisper_params_tmp.no_timestamps);
	obs_data_set_default_bool(s, "single_segment", whisper_params_tmp.single_segment);
	obs_data_set_default_bool(s, "print_special", false);
	obs_data_set_default_bool(s, "print_progress", false);
	obs_data_set_default_bool(s, "print_realtime", false);
	obs_data_set_default_bool(s, "print_timestamps", false);
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
	// obs_data_set_default_string(s, "language", whisper_params_tmp.language);
	obs_data_set_default_bool(s, "detect_language", whisper_params_tmp.detect_language);
	obs_data_set_default_bool(s, "suppress_blank", false);
	obs_data_set_default_bool(s, "suppress_nst", false);
	obs_data_set_default_double(s, "temperature", whisper_params_tmp.temperature);
	obs_data_set_default_double(s, "max_initial_ts", whisper_params_tmp.max_initial_ts);
	obs_data_set_default_double(s, "length_penalty", whisper_params_tmp.length_penalty);
	obs_data_set_default_double(s, "temperature_inc", whisper_params_tmp.temperature_inc);
	obs_data_set_default_double(s, "entropy_thold", whisper_params_tmp.entropy_thold);
	obs_data_set_default_double(s, "logprob_thold", whisper_params_tmp.logprob_thold);
	obs_data_set_default_double(s, "no_speech_thold", whisper_params_tmp.no_speech_thold);
	obs_data_set_default_int(s, "greedy.best_of", whisper_params_tmp.greedy.best_of);
	obs_data_set_default_int(s, "beam_search.beam_size",
				 whisper_params_tmp.beam_search.beam_size);
	obs_data_set_default_double(s, "beam_search.patience",
				    whisper_params_tmp.beam_search.patience);
}

void apply_whisper_params_from_settings(whisper_full_params &params, obs_data_t *settings)
{
	params = whisper_full_default_params(
		(whisper_sampling_strategy)obs_data_get_int(settings, "strategy"));
	params.n_threads = (int)obs_data_get_int(settings, "n_threads");
	params.n_max_text_ctx = (int)obs_data_get_int(settings, "n_max_text_ctx");
	params.offset_ms = (int)obs_data_get_int(settings, "offset_ms");
	params.duration_ms = (int)obs_data_get_int(settings, "duration_ms");
	params.translate = obs_data_get_bool(settings, "whisper_translate");
	params.no_context = obs_data_get_bool(settings, "no_context");
	params.no_timestamps = obs_data_get_bool(settings, "no_timestamps");
	params.single_segment = obs_data_get_bool(settings, "single_segment");
	params.print_special = obs_data_get_bool(settings, "print_special");
	params.print_progress = obs_data_get_bool(settings, "print_progress");
	params.print_realtime = obs_data_get_bool(settings, "print_realtime");
	params.print_timestamps = obs_data_get_bool(settings, "print_timestamps");
	params.token_timestamps = obs_data_get_bool(settings, "token_timestamps");
	params.thold_pt = (float)obs_data_get_double(settings, "thold_pt");
	params.thold_ptsum = (float)obs_data_get_double(settings, "thold_ptsum");
	params.max_len = (int)obs_data_get_int(settings, "max_len");
	params.split_on_word = obs_data_get_bool(settings, "split_on_word");
	params.max_tokens = (int)obs_data_get_int(settings, "max_tokens");
	params.debug_mode = obs_data_get_bool(settings, "debug_mode");
	params.audio_ctx = (int)obs_data_get_int(settings, "audio_ctx");
	params.tdrz_enable = obs_data_get_bool(settings, "tdrz_enable");
	params.suppress_regex = obs_data_get_string(settings, "suppress_regex");
	params.initial_prompt = obs_data_get_string(settings, "initial_prompt");
	// params.language = obs_data_get_string(settings, "language");
	params.detect_language = obs_data_get_bool(settings, "detect_language");
	params.suppress_blank = obs_data_get_bool(settings, "suppress_blank");
	params.suppress_nst = obs_data_get_bool(settings, "suppress_nst");
	params.temperature = (float)obs_data_get_double(settings, "temperature");
	params.max_initial_ts = (float)obs_data_get_double(settings, "max_initial_ts");
	params.length_penalty = (float)obs_data_get_double(settings, "length_penalty");
	params.temperature_inc = (float)obs_data_get_double(settings, "temperature_inc");
	params.entropy_thold = (float)obs_data_get_double(settings, "entropy_thold");
	params.logprob_thold = (float)obs_data_get_double(settings, "logprob_thold");
	params.no_speech_thold = (float)obs_data_get_double(settings, "no_speech_thold");
	params.greedy.best_of = (int)obs_data_get_int(settings, "greedy.best_of");
	params.beam_search.beam_size = (int)obs_data_get_int(settings, "beam_search.beam_size");
	params.beam_search.patience = (float)obs_data_get_double(settings, "beam_search.patience");
}

void add_whisper_params_group_properties(obs_properties_t *ppts)
{
	obs_properties_t *g = obs_properties_create();
	obs_properties_add_group(ppts, "whisper_params_group", MT_("whisper_parameters"),
				 OBS_GROUP_NORMAL, g);

	obs_properties_add_list(g, "strategy", MT_("whisper_sampling_strategy"),
				OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_properties_add_int(g, "n_threads", MT_("n_threads"), 1, 8, 1);
	obs_properties_add_int(g, "n_max_text_ctx", MT_("n_max_text_ctx"), 1, 100, 1);
	obs_properties_add_int(g, "offset_ms", MT_("offset_ms"), 0, 10000, 100);
	obs_properties_add_int(g, "duration_ms", MT_("duration_ms"), 0, 30000, 500);
	obs_properties_add_bool(g, "whisper_translate", MT_("whisper_translate"));
	obs_properties_add_bool(g, "no_context", MT_("no_context"));
	obs_properties_add_bool(g, "no_timestamps", MT_("no_timestamps"));
	obs_properties_add_bool(g, "single_segment", MT_("single_segment"));
	obs_properties_add_bool(g, "print_special", MT_("print_special"));
	obs_properties_add_bool(g, "print_progress", MT_("print_progress"));
	obs_properties_add_bool(g, "print_realtime", MT_("print_realtime"));
	obs_properties_add_bool(g, "print_timestamps", MT_("print_timestamps"));
	obs_properties_add_bool(g, "token_timestamps", MT_("token_timestamps"));
	obs_properties_add_float(g, "thold_pt", MT_("thold_pt"), 0, 1, 0.05);
	obs_properties_add_float(g, "thold_ptsum", MT_("thold_ptsum"), 0, 1, 0.05);
	obs_properties_add_int(g, "max_len", MT_("max_len"), 0, 1000, 1);
	obs_properties_add_bool(g, "split_on_word", MT_("split_on_word"));
	obs_properties_add_int(g, "max_tokens", MT_("max_tokens"), 0, 1000, 1);
	obs_properties_add_bool(g, "debug_mode", MT_("debug_mode"));
	obs_properties_add_int(g, "audio_ctx", MT_("audio_ctx"), 0, 10, 1);
	obs_properties_add_bool(g, "tdrz_enable", MT_("tdrz_enable"));
	obs_properties_add_text(g, "suppress_regex", MT_("suppress_regex"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(g, "initial_prompt", MT_("initial_prompt"), OBS_TEXT_DEFAULT);
	// obs_properties_add_text(g, "language", MT_("language"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(g, "detect_language", MT_("detect_language"));
	obs_properties_add_bool(g, "suppress_blank", MT_("suppress_blank"));
	obs_properties_add_bool(g, "suppress_nst", MT_("suppress_nst"));
	obs_properties_add_float(g, "temperature", MT_("temperature"), 0, 1, 0.05);
	obs_properties_add_float(g, "max_initial_ts", MT_("max_initial_ts"), 0, 100, 1);
	obs_properties_add_float(g, "length_penalty", MT_("length_penalty"), 0, 1, 0.05);
	obs_properties_add_float(g, "temperature_inc", MT_("temperature_inc"), 0, 1, 0.05);
	obs_properties_add_float(g, "entropy_thold", MT_("entropy_thold"), 0, 1, 0.05);
	obs_properties_add_float(g, "logprob_thold", MT_("logprob_thold"), 0, 1, 0.05);
	obs_properties_add_float(g, "no_speech_thold", MT_("no_speech_thold"), 0, 1, 0.05);
	obs_properties_add_int(g, "greedy.best_of", MT_("greedy.best_of"), 1, 10, 1);
	obs_properties_add_int(g, "beam_search.beam_size", MT_("beam_search.beam_size"), 1, 10, 1);
	obs_properties_add_float(g, "beam_search.patience", MT_("beam_search.patience"), 0, 1,
				 0.05);
}
