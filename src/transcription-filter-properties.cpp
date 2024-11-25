
#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include "transcription-filter-data.h"
#include "transcription-filter.h"
#include "transcription-filter-utils.h"
#include "whisper-utils/whisper-language.h"
#include "whisper-utils/vad-processing.h"
#include "whisper-utils/whisper-params.h"
#include "model-utils/model-downloader-types.h"
#include "translation/language_codes.h"
#include "ui/filter-replace-dialog.h"
#include "ui/filter-replace-utils.h"

#include <string>
#include <vector>
#include "whisper-utils/whisper-utils.h"

bool translation_options_callback(obs_properties_t *props, obs_property_t *property,
				  obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	// Show/Hide the translation group
	const bool translate_enabled = obs_data_get_bool(settings, "translate");
	const bool is_advanced = obs_data_get_int(settings, "advanced_settings_mode") == 1;
	for (const auto &prop :
	     {"translate_target_language", "translate_model", "translate_output"}) {
		obs_property_set_visible(obs_properties_get(props, prop), translate_enabled);
	}
	for (const auto &prop :
	     {"translate_add_context", "translate_input_tokenization_style",
	      "translation_sampling_temperature", "translation_repetition_penalty",
	      "translation_beam_size", "translation_max_decoding_length",
	      "translation_no_repeat_ngram_size", "translation_max_input_length",
	      "translate_only_full_sentences"}) {
		obs_property_set_visible(obs_properties_get(props, prop),
					 translate_enabled && is_advanced);
	}
	const bool is_external =
		(strcmp(obs_data_get_string(settings, "translate_model"), "!!!external!!!") == 0);
	obs_property_set_visible(obs_properties_get(props, "translation_model_path_external"),
				 is_external && translate_enabled);
	return true;
}

bool translation_cloud_provider_selection_callback(obs_properties_t *props, obs_property_t *p,
						   obs_data_t *s)
{
	UNUSED_PARAMETER(p);
	const char *provider = obs_data_get_string(s, "translate_cloud_provider");
	// show the access key for all except the custom provider
	obs_property_set_visible(obs_properties_get(props, "translate_cloud_api_key"),
				 strcmp(provider, "api") != 0);
	obs_property_set_visible(obs_properties_get(props, "translate_cloud_deepl_free"),
				 strcmp(provider, "deepl") == 0);
	// show the secret key input for the papago provider only
	obs_property_set_visible(obs_properties_get(props, "translate_cloud_secret_key"),
				 strcmp(provider, "papago") == 0);
	// show the region input for the azure provider only
	obs_property_set_visible(obs_properties_get(props, "translate_cloud_region"),
				 strcmp(provider, "azure") == 0);
	// show the endpoint and body input for the custom provider only
	obs_property_set_visible(obs_properties_get(props, "translate_cloud_endpoint"),
				 strcmp(provider, "api") == 0);
	obs_property_set_visible(obs_properties_get(props, "translate_cloud_body"),
				 strcmp(provider, "api") == 0);
	// show the response json path input for the custom provider only
	obs_property_set_visible(obs_properties_get(props, "translate_cloud_response_json_path"),
				 strcmp(provider, "api") == 0);
	return true;
}

bool translation_cloud_options_callback(obs_properties_t *props, obs_property_t *property,
					obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	// Show/Hide the cloud translation group options
	const bool translate_enabled = obs_data_get_bool(settings, "translate_cloud");
	for (const auto &prop :
	     {"translate_cloud_provider", "translate_cloud_target_language",
	      "translate_cloud_output", "translate_cloud_api_key",
	      "translate_cloud_only_full_sentences", "translate_cloud_secret_key",
	      "translate_cloud_deepl_free", "translate_cloud_region", "translate_cloud_endpoint",
	      "translate_cloud_body", "translate_cloud_response_json_path"}) {
		obs_property_set_visible(obs_properties_get(props, prop), translate_enabled);
	}
	if (translate_enabled) {
		translation_cloud_provider_selection_callback(props, NULL, settings);
	}
	return true;
}

bool advanced_settings_callback(obs_properties_t *props, obs_property_t *property,
				obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	// If advanced settings is enabled, show the advanced settings group
	const bool show_hide = obs_data_get_int(settings, "advanced_settings_mode") == 1;
	for (const std::string &prop_name :
	     {"whisper_params_group", "buffered_output_group", "log_group", "advanced_group",
	      "file_output_enable", "partial_group"}) {
		obs_property_set_visible(obs_properties_get(props, prop_name.c_str()), show_hide);
	}
	translation_options_callback(props, NULL, settings);
	translation_cloud_options_callback(props, NULL, settings);
	return true;
}

bool file_output_select_changed(obs_properties_t *props, obs_property_t *property,
				obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	// Show or hide the output filename selection input
	const bool show_hide = obs_data_get_bool(settings, "file_output_enable");
	for (const std::string &prop_name :
	     {"subtitle_output_filename", "subtitle_save_srt", "truncate_output_file",
	      "only_while_recording", "rename_file_to_match_recording", "file_output_info"}) {
		obs_property_set_visible(obs_properties_get(props, prop_name.c_str()), show_hide);
	}
	return true;
}

bool external_model_file_selection(void *data_, obs_properties_t *props, obs_property_t *property,
				   obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	struct transcription_filter_data *gf_ =
		static_cast<struct transcription_filter_data *>(data_);
	// If the selected model is the external model, show the external model file selection
	// input
	const char *new_model_path_cstr =
		obs_data_get_string(settings, "whisper_model_path") != nullptr
			? obs_data_get_string(settings, "whisper_model_path")
			: "";
	const std::string new_model_path = new_model_path_cstr;
	const bool is_external = (new_model_path.find("!!!external!!!") != std::string::npos);
	if (is_external) {
		obs_property_set_visible(obs_properties_get(props, "whisper_model_path_external"),
					 true);
	} else {
		obs_property_set_visible(obs_properties_get(props, "whisper_model_path_external"),
					 false);
	}

	// check if this is a new model selection
	if (gf_->whisper_model_loaded_new) {
		// if the model is english-only -> hide all the languages but english
		const bool is_english_only_internal =
			(new_model_path.find("English") != std::string::npos) && !is_external;
		// clear the language selection list ("whisper_language_select")
		obs_property_t *prop_lang = obs_properties_get(props, "whisper_language_select");
		obs_property_list_clear(prop_lang);
		if (is_english_only_internal) {
			// add only the english language
			obs_property_list_add_string(prop_lang, "English", "en");
			// set the language to english
			obs_data_set_string(settings, "whisper_language_select", "en");
		} else {
			// add all the languages
			for (const auto &lang : whisper_available_lang) {
				obs_property_list_add_string(prop_lang, lang.second.c_str(),
							     lang.first.c_str());
			}
			// set the language to auto (default)
			obs_data_set_string(settings, "whisper_language_select", "auto");
		}
		gf_->whisper_model_loaded_new = false;
	}
	return true;
}

bool translation_external_model_selection(obs_properties_t *props, obs_property_t *property,
					  obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	// If the selected model is the external model, show the external model file selection
	// input
	const char *new_model_path = obs_data_get_string(settings, "translate_model");
	const bool is_external = (strcmp(new_model_path, "!!!external!!!") == 0);
	const bool is_whisper = (strcmp(new_model_path, "whisper-based-translation") == 0);
	const bool is_advanced = obs_data_get_int(settings, "advanced_settings_mode") == 1;
	obs_property_set_visible(obs_properties_get(props, "translation_model_path_external"),
				 is_external);
	obs_property_set_visible(obs_properties_get(props, "translate_add_context"),
				 !is_whisper && is_advanced);
	obs_property_set_visible(obs_properties_get(props, "translate_input_tokenization_style"),
				 !is_whisper && is_advanced);
	obs_property_set_visible(obs_properties_get(props, "translate_output"), !is_whisper);
	return true;
}

void add_transcription_group_properties(obs_properties_t *ppts,
					struct transcription_filter_data *gf)
{
	// add "Transcription" group
	obs_properties_t *transcription_group = obs_properties_create();
	obs_properties_add_group(ppts, "transcription_group", MT_("transcription_group"),
				 OBS_GROUP_NORMAL, transcription_group);

	// Add a list of available whisper models to download
	obs_property_t *whisper_models_list = obs_properties_add_list(
		transcription_group, "whisper_model_path", MT_("whisper_model"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(whisper_models_list, "Load external model file",
				     "!!!external!!!");
	// Add models from models_info map
	for (const auto &model_info : get_sorted_models_info()) {
		if (model_info.type == MODEL_TYPE_TRANSCRIPTION) {
			obs_property_list_add_string(whisper_models_list,
						     model_info.friendly_name.c_str(),
						     model_info.friendly_name.c_str());
		}
	}

	// Add a file selection input to select an external model file
	obs_properties_add_path(transcription_group, "whisper_model_path_external",
				MT_("external_model_file"), OBS_PATH_FILE, "Model (*.bin)", NULL);
	// Hide the external model file selection input
	obs_property_set_visible(obs_properties_get(ppts, "whisper_model_path_external"), false);

	// Add a callback to the model list to handle the external model file selection
	obs_property_set_modified_callback2(whisper_models_list, external_model_file_selection, gf);
}

void add_translation_cloud_group_properties(obs_properties_t *ppts)
{
	// add translation cloud group
	obs_properties_t *translation_cloud_group = obs_properties_create();
	obs_property_t *translation_cloud_group_prop =
		obs_properties_add_group(ppts, "translate_cloud", MT_("translate_cloud"),
					 OBS_GROUP_CHECKABLE, translation_cloud_group);

	obs_property_set_modified_callback(translation_cloud_group_prop,
					   translation_cloud_options_callback);

	// add explaination text
	obs_properties_add_text(translation_cloud_group, "translate_cloud_explaination",
				MT_("translate_cloud_explaination"), OBS_TEXT_INFO);

	// add cloud translation service provider selection
	obs_property_t *prop_translate_cloud_provider = obs_properties_add_list(
		translation_cloud_group, "translate_cloud_provider",
		MT_("translate_cloud_provider"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// Populate the dropdown with the cloud translation service providers
	obs_property_list_add_string(prop_translate_cloud_provider, MT_("Google-Cloud-Translation"),
				     "google");
	obs_property_list_add_string(prop_translate_cloud_provider, MT_("Microsoft-Translator"),
				     "azure");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("Amazon-Translate"),
	// 			     "amazon-translate");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("IBM-Watson-Translate"),
	// 			     "ibm-watson-translate");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("Yandex-Translate"),
	// 			     "yandex-translate");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("Baidu-Translate"),
	// 			     "baidu-translate");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("Tencent-Translate"),
	// 			     "tencent-translate");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("Alibaba-Translate"),
	// 			     "alibaba-translate");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("Naver-Translate"),
	// 			     "naver-translate");
	// obs_property_list_add_string(prop_translate_cloud_provider, MT_("Kakao-Translate"),
	// 			     "kakao-translate");
	obs_property_list_add_string(prop_translate_cloud_provider, MT_("Papago-Translate"),
				     "papago");
	obs_property_list_add_string(prop_translate_cloud_provider, MT_("Deepl-Translate"),
				     "deepl");
	obs_property_list_add_string(prop_translate_cloud_provider, MT_("OpenAI-Translate"),
				     "openai");
	obs_property_list_add_string(prop_translate_cloud_provider, MT_("Claude-Translate"),
				     "claude");
	obs_property_list_add_string(prop_translate_cloud_provider, MT_("API-Translate"), "api");

	// add callback to show/hide the free API option for deepl
	obs_property_set_modified_callback(prop_translate_cloud_provider,
					   translation_cloud_provider_selection_callback);

	// add target language selection
	obs_property_t *prop_tgt = obs_properties_add_list(
		translation_cloud_group, "translate_cloud_target_language", MT_("target_language"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// Populate the dropdown with the language codes
	for (const auto &language : language_codes) {
		obs_property_list_add_string(prop_tgt, language.second.c_str(),
					     language.first.c_str());
	}
	// add option for routing the translation to an output source
	obs_property_t *prop_output = obs_properties_add_list(
		translation_cloud_group, "translate_cloud_output", MT_("translate_output"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(prop_output, "Write to captions output", "none");
	obs_enum_sources(add_sources_to_list, prop_output);

	// add boolean option for only full sentences
	obs_properties_add_bool(translation_cloud_group, "translate_cloud_only_full_sentences",
				MT_("translate_cloud_only_full_sentences"));

	// add input for API Key
	obs_properties_add_text(translation_cloud_group, "translate_cloud_api_key",
				MT_("translate_cloud_api_key"), OBS_TEXT_DEFAULT);
	// add input for secret key
	obs_properties_add_text(translation_cloud_group, "translate_cloud_secret_key",
				MT_("translate_cloud_secret_key"), OBS_TEXT_PASSWORD);

	// add boolean option for free API from deepl
	obs_properties_add_bool(translation_cloud_group, "translate_cloud_deepl_free",
				MT_("translate_cloud_deepl_free"));

	// add translate_cloud_region for azure
	obs_properties_add_text(translation_cloud_group, "translate_cloud_region",
				MT_("translate_cloud_region"), OBS_TEXT_DEFAULT);

	// add input for API endpoint
	obs_properties_add_text(translation_cloud_group, "translate_cloud_endpoint",
				MT_("translate_cloud_endpoint"), OBS_TEXT_DEFAULT);
	// add input for API body
	obs_properties_add_text(translation_cloud_group, "translate_cloud_body",
				MT_("translate_cloud_body"), OBS_TEXT_MULTILINE);
	// add input for json response path
	obs_properties_add_text(translation_cloud_group, "translate_cloud_response_json_path",
				MT_("translate_cloud_response_json_path"), OBS_TEXT_DEFAULT);
}

void add_translation_group_properties(obs_properties_t *ppts)
{
	// add translation option group
	obs_properties_t *translation_group = obs_properties_create();
	obs_property_t *translation_group_prop = obs_properties_add_group(
		ppts, "translate", MT_("translate_local"), OBS_GROUP_CHECKABLE, translation_group);

	// add explaination text
	obs_properties_add_text(translation_group, "translate_explaination",
				MT_("translate_explaination"), OBS_TEXT_INFO);

	// add translation model selection
	obs_property_t *prop_translate_model = obs_properties_add_list(
		translation_group, "translate_model", MT_("translate_model"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	// Populate the dropdown with the translation models
	// add "Whisper-Based Translation" option
	obs_property_list_add_string(prop_translate_model, MT_("Whisper-Based-Translation"),
				     "whisper-based-translation");
	for (const auto &model_info : models_info()) {
		if (model_info.second.type == MODEL_TYPE_TRANSLATION) {
			obs_property_list_add_string(prop_translate_model, model_info.first.c_str(),
						     model_info.first.c_str());
		}
	}
	// add external model option
	obs_property_list_add_string(prop_translate_model, MT_("load_external_model"),
				     "!!!external!!!");
	// add callback to handle the external model file selection
	obs_properties_add_path(translation_group, "translation_model_path_external",
				MT_("external_model_folder"), OBS_PATH_DIRECTORY,
				"CT2 Model folder", NULL);
	// Hide the external model file selection input
	obs_property_set_visible(obs_properties_get(ppts, "translation_model_path_external"),
				 false);
	// Add a callback to the model list to handle the external model file selection
	obs_property_set_modified_callback(prop_translate_model,
					   translation_external_model_selection);
	// add target language selection
	obs_property_t *prop_tgt = obs_properties_add_list(
		translation_group, "translate_target_language", MT_("target_language"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	// add slider for number of context lines to add to the translation
	obs_properties_add_int_slider(translation_group, "translate_add_context",
				      MT_("translate_add_context"), 0, 5, 1);
	obs_properties_add_bool(translation_group, "translate_only_full_sentences",
				MT_("translate_only_full_sentences"));

	// Populate the dropdown with the language codes
	for (const auto &language : language_codes) {
		obs_property_list_add_string(prop_tgt, language.second.c_str(),
					     language.first.c_str());
	}
	// add option for routing the translation to an output source
	obs_property_t *prop_output = obs_properties_add_list(translation_group, "translate_output",
							      MT_("translate_output"),
							      OBS_COMBO_TYPE_LIST,
							      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(prop_output, "Write to captions output", "none");
	obs_enum_sources(add_sources_to_list, prop_output);

	// add callback to enable/disable translation group
	obs_property_set_modified_callback(translation_group_prop, translation_options_callback);
	// add tokenization style options
	obs_property_t *prop_token_style =
		obs_properties_add_list(translation_group, "translate_input_tokenization_style",
					MT_("translate_input_tokenization_style"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop_token_style, "M2M100 Tokens", INPUT_TOKENIZAION_M2M100);
	obs_property_list_add_int(prop_token_style, "T5 Tokens", INPUT_TOKENIZAION_T5);

	// add translation options: beam_size, max_decoding_length, repetition_penalty, no_repeat_ngram_size, max_input_length, sampling_temperature
	obs_properties_add_float_slider(translation_group, "translation_sampling_temperature",
					MT_("translation_sampling_temperature"), 0.0, 1.0, 0.05);
	obs_properties_add_float_slider(translation_group, "translation_repetition_penalty",
					MT_("translation_repetition_penalty"), 1.0, 5.0, 0.25);
	obs_properties_add_int_slider(translation_group, "translation_beam_size",
				      MT_("translation_beam_size"), 1, 10, 1);
	obs_properties_add_int_slider(translation_group, "translation_max_decoding_length",
				      MT_("translation_max_decoding_length"), 1, 100, 5);
	obs_properties_add_int_slider(translation_group, "translation_max_input_length",
				      MT_("translation_max_input_length"), 1, 100, 5);
	obs_properties_add_int_slider(translation_group, "translation_no_repeat_ngram_size",
				      MT_("translation_no_repeat_ngram_size"), 1, 10, 1);
}

void add_file_output_group_properties(obs_properties_t *ppts)
{
	// create a file output group
	obs_properties_t *file_output_group = obs_properties_create();
	// add a checkbox group for file output
	obs_property_t *file_output_group_prop =
		obs_properties_add_group(ppts, "file_output_enable", MT_("file_output_group"),
					 OBS_GROUP_CHECKABLE, file_output_group);

	obs_properties_add_path(file_output_group, "subtitle_output_filename",
				MT_("output_filename"), OBS_PATH_FILE_SAVE, "Text (*.txt)", NULL);
	// add info text about the file output
	obs_properties_add_text(file_output_group, "file_output_info", MT_("file_output_info"),
				OBS_TEXT_INFO);
	obs_properties_add_bool(file_output_group, "subtitle_save_srt", MT_("save_srt"));
	obs_properties_add_bool(file_output_group, "truncate_output_file",
				MT_("truncate_output_file"));
	obs_properties_add_bool(file_output_group, "only_while_recording",
				MT_("only_while_recording"));
	obs_properties_add_bool(file_output_group, "rename_file_to_match_recording",
				MT_("rename_file_to_match_recording"));
	obs_property_set_modified_callback(file_output_group_prop, file_output_select_changed);
}

void add_buffered_output_group_properties(obs_properties_t *ppts)
{
	// add buffered output options group
	obs_properties_t *buffered_output_group = obs_properties_create();
	obs_properties_add_group(ppts, "buffered_output_group", MT_("buffered_output_parameters"),
				 OBS_GROUP_NORMAL, buffered_output_group);
	obs_properties_add_bool(buffered_output_group, "buffered_output", MT_("buffered_output"));
	// add buffer "type" character or word
	obs_property_t *buffer_type_list = obs_properties_add_list(
		buffered_output_group, "buffer_output_type", MT_("buffer_output_type"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(buffer_type_list, "Character", SEGMENTATION_TOKEN);
	obs_property_list_add_int(buffer_type_list, "Word", SEGMENTATION_WORD);
	obs_property_list_add_int(buffer_type_list, "Sentence", SEGMENTATION_SENTENCE);
	// add callback to the segmentation selection to set default values
	obs_property_set_modified_callback(buffer_type_list, [](obs_properties_t *props,
								obs_property_t *property,
								obs_data_t *settings) {
		UNUSED_PARAMETER(property);
		UNUSED_PARAMETER(props);
		const int segmentation_type = (int)obs_data_get_int(settings, "buffer_output_type");
		// set default values for the number of lines and characters per line
		switch (segmentation_type) {
		case SEGMENTATION_TOKEN:
			obs_data_set_int(settings, "buffer_num_lines", 2);
			obs_data_set_int(settings, "buffer_num_chars_per_line", 30);
			break;
		case SEGMENTATION_WORD:
			obs_data_set_int(settings, "buffer_num_lines", 2);
			obs_data_set_int(settings, "buffer_num_chars_per_line", 10);
			break;
		case SEGMENTATION_SENTENCE:
			obs_data_set_int(settings, "buffer_num_lines", 2);
			obs_data_set_int(settings, "buffer_num_chars_per_line", 2);
			break;
		}
		return true;
	});
	// add buffer lines parameter
	obs_properties_add_int_slider(buffered_output_group, "buffer_num_lines",
				      MT_("buffer_num_lines"), 1, 5, 1);
	// add buffer number of characters per line parameter
	obs_properties_add_int_slider(buffered_output_group, "buffer_num_chars_per_line",
				      MT_("buffer_num_chars_per_line"), 1, 100, 1);
}

void add_advanced_group_properties(obs_properties_t *ppts, struct transcription_filter_data *gf)
{
	// add a group for advanced configuration
	obs_properties_t *advanced_config_group = obs_properties_create();
	obs_properties_add_group(ppts, "advanced_group", MT_("advanced_group"), OBS_GROUP_NORMAL,
				 advanced_config_group);

	obs_properties_add_bool(advanced_config_group, "caption_to_stream",
				MT_("caption_to_stream"));

	obs_properties_add_int_slider(advanced_config_group, "min_sub_duration",
				      MT_("min_sub_duration"), 1000, 5000, 50);
	obs_properties_add_int_slider(advanced_config_group, "max_sub_duration",
				      MT_("max_sub_duration"), 1000, 5000, 50);
	obs_properties_add_float_slider(advanced_config_group, "sentence_psum_accept_thresh",
					MT_("sentence_psum_accept_thresh"), 0.0, 1.0, 0.05);

	obs_properties_add_bool(advanced_config_group, "process_while_muted",
				MT_("process_while_muted"));

	// add selection for Active VAD vs Hybrid VAD
	obs_property_t *vad_mode_list =
		obs_properties_add_list(advanced_config_group, "vad_mode", MT_("vad_mode"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(vad_mode_list, MT_("No_VAD"), VAD_MODE_DISABLED);
	obs_property_list_add_int(vad_mode_list, MT_("Active_VAD"), VAD_MODE_ACTIVE);
	obs_property_list_add_int(vad_mode_list, MT_("Hybrid_VAD"), VAD_MODE_HYBRID);
	// add vad threshold slider
	obs_properties_add_float_slider(advanced_config_group, "vad_threshold",
					MT_("vad_threshold"), 0.0, 1.0, 0.05);
	// add duration filter threshold slider
	obs_properties_add_float_slider(advanced_config_group, "duration_filter_threshold",
					MT_("duration_filter_threshold"), 0.1, 3.0, 0.05);
	// add segment duration slider
	obs_properties_add_int_slider(advanced_config_group, "segment_duration",
				      MT_("segment_duration"), 3000, 15000, 100);

	// add button to open filter and replace UI dialog
	obs_properties_add_button2(
		advanced_config_group, "open_filter_ui", MT_("open_filter_ui"),
		[](obs_properties_t *props, obs_property_t *property, void *data_) {
			UNUSED_PARAMETER(props);
			UNUSED_PARAMETER(property);
			struct transcription_filter_data *gf_ =
				static_cast<struct transcription_filter_data *>(data_);
			FilterReplaceDialog *filter_replace_dialog = new FilterReplaceDialog(
				(QWidget *)obs_frontend_get_main_window(), gf_);
			filter_replace_dialog->exec();
			// store the filter data on the source settings
			obs_data_t *settings = obs_source_get_settings(gf_->context);
			// serialize the filter data
			const std::string filter_data =
				serialize_filter_words_replace(gf_->filter_words_replace);
			obs_data_set_string(settings, "filter_words_replace", filter_data.c_str());
			obs_data_release(settings);
			return true;
		},
		gf);
}

void add_logging_group_properties(obs_properties_t *ppts)
{
	// add a group for Logging options
	obs_properties_t *log_group = obs_properties_create();
	obs_properties_add_group(ppts, "log_group", MT_("log_group"), OBS_GROUP_NORMAL, log_group);

	obs_properties_add_bool(log_group, "log_words", MT_("log_words"));
	obs_property_t *list = obs_properties_add_list(log_group, "log_level", MT_("log_level"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, "DEBUG (Won't show)", LOG_DEBUG);
	obs_property_list_add_int(list, "INFO", LOG_INFO);
	obs_property_list_add_int(list, "WARNING", LOG_WARNING);
}

void add_general_group_properties(obs_properties_t *ppts)
{
	// add "General" group
	obs_properties_t *general_group = obs_properties_create();
	obs_properties_add_group(ppts, "general_group", MT_("general_group"), OBS_GROUP_NORMAL,
				 general_group);

	obs_property_t *subs_output =
		obs_properties_add_list(general_group, "subtitle_sources", MT_("subtitle_sources"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// Add "none" option
	obs_property_list_add_string(subs_output, MT_("none_no_output"), "none");
	// Add text sources
	obs_enum_sources(add_sources_to_list, subs_output);

	// Add language selector
	obs_property_t *whisper_language_select_list =
		obs_properties_add_list(general_group, "whisper_language_select", MT_("language"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// iterate over all available languages and add them to the list
	for (auto const &pair : whisper_available_lang_reverse) {
		obs_property_list_add_string(whisper_language_select_list, pair.first.c_str(),
					     pair.second.c_str());
	}
}

void add_partial_group_properties(obs_properties_t *ppts)
{
	// add a group for partial transcription
	obs_properties_t *partial_group = obs_properties_create();
	obs_properties_add_group(ppts, "partial_group", MT_("partial_transcription"),
				 OBS_GROUP_CHECKABLE, partial_group);

	// add text info
	obs_properties_add_text(partial_group, "partial_info", MT_("partial_transcription_info"),
				OBS_TEXT_INFO);

	// add slider for partial latecy
	obs_properties_add_int_slider(partial_group, "partial_latency", MT_("partial_latency"), 500,
				      3000, 50);
}

obs_properties_t *transcription_filter_properties(void *data)
{
	struct transcription_filter_data *gf =
		static_cast<struct transcription_filter_data *>(data);

	obs_properties_t *ppts = obs_properties_create();

	// add a drop down selection for advanced vs simple settings
	obs_property_t *advanced_settings = obs_properties_add_list(ppts, "advanced_settings_mode",
								    MT_("advanced_settings_mode"),
								    OBS_COMBO_TYPE_LIST,
								    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(advanced_settings, MT_("simple_mode"), 0);
	obs_property_list_add_int(advanced_settings, MT_("advanced_mode"), 1);
	obs_property_set_modified_callback(advanced_settings, advanced_settings_callback);

	add_general_group_properties(ppts);
	add_transcription_group_properties(ppts, gf);
	add_translation_group_properties(ppts);
	add_translation_cloud_group_properties(ppts);
	add_file_output_group_properties(ppts);
	add_buffered_output_group_properties(ppts);
	add_advanced_group_properties(ppts, gf);
	add_logging_group_properties(ppts);
	add_partial_group_properties(ppts);
	add_whisper_params_group_properties(ppts);

	// Add a informative text about the plugin
	obs_properties_add_text(
		ppts, "info",
		QString(PLUGIN_INFO_TEMPLATE).arg(PLUGIN_VERSION).toStdString().c_str(),
		OBS_TEXT_INFO);

	UNUSED_PARAMETER(data);
	return ppts;
}

void transcription_filter_defaults(obs_data_t *s)
{
	obs_log(LOG_DEBUG, "filter defaults");

	obs_data_set_default_bool(s, "buffered_output", false);
	obs_data_set_default_int(s, "buffer_num_lines", 2);
	obs_data_set_default_int(s, "buffer_num_chars_per_line", 30);
	obs_data_set_default_int(s, "buffer_output_type",
				 (int)TokenBufferSegmentation::SEGMENTATION_TOKEN);

	obs_data_set_default_bool(s, "vad_mode", VAD_MODE_ACTIVE);
	obs_data_set_default_double(s, "vad_threshold", 0.65);
	obs_data_set_default_double(s, "duration_filter_threshold", 2.25);
	obs_data_set_default_int(s, "segment_duration", 7000);
	obs_data_set_default_int(s, "log_level", LOG_DEBUG);
	obs_data_set_default_bool(s, "log_words", false);
	obs_data_set_default_bool(s, "caption_to_stream", false);
	obs_data_set_default_string(s, "whisper_model_path", "Whisper Tiny English (74Mb)");
	obs_data_set_default_string(s, "whisper_language_select", "en");
	obs_data_set_default_string(s, "subtitle_sources", "none");
	obs_data_set_default_bool(s, "process_while_muted", false);
	obs_data_set_default_bool(s, "subtitle_save_srt", false);
	obs_data_set_default_bool(s, "truncate_output_file", false);
	obs_data_set_default_bool(s, "only_while_recording", false);
	obs_data_set_default_bool(s, "rename_file_to_match_recording", true);
	obs_data_set_default_int(s, "min_sub_duration", 1000);
	obs_data_set_default_int(s, "max_sub_duration", 3000);
	obs_data_set_default_bool(s, "advanced_settings", false);
	obs_data_set_default_double(s, "sentence_psum_accept_thresh", 0.4);
	obs_data_set_default_bool(s, "partial_group", true);
	obs_data_set_default_int(s, "partial_latency", 1100);

	// translation options
	obs_data_set_default_bool(s, "translate", false);
	obs_data_set_default_string(s, "translate_target_language", "__es__");
	obs_data_set_default_int(s, "translate_add_context", 1);
	obs_data_set_default_bool(s, "translate_only_full_sentences", true);
	obs_data_set_default_string(s, "translate_model", "whisper-based-translation");
	obs_data_set_default_string(s, "translation_model_path_external", "");
	obs_data_set_default_int(s, "translate_input_tokenization_style", INPUT_TOKENIZAION_M2M100);
	obs_data_set_default_double(s, "translation_sampling_temperature", 0.1);
	obs_data_set_default_double(s, "translation_repetition_penalty", 2.0);
	obs_data_set_default_int(s, "translation_beam_size", 1);
	obs_data_set_default_int(s, "translation_max_decoding_length", 65);
	obs_data_set_default_int(s, "translation_no_repeat_ngram_size", 1);
	obs_data_set_default_int(s, "translation_max_input_length", 65);

	// cloud translation options
	obs_data_set_default_bool(s, "translate_cloud", false);
	obs_data_set_default_string(s, "translate_cloud_provider", "google");
	obs_data_set_default_string(s, "translate_cloud_target_language", "en");
	obs_data_set_default_string(s, "translate_cloud_output", "none");
	obs_data_set_default_bool(s, "translate_cloud_only_full_sentences", true);
	obs_data_set_default_string(s, "translate_cloud_api_key", "");
	obs_data_set_default_string(s, "translate_cloud_secret_key", "");
	obs_data_set_default_bool(s, "translate_cloud_deepl_free", true);
	obs_data_set_default_string(s, "translate_cloud_region", "eastus");
	obs_data_set_default_string(s, "translate_cloud_endpoint",
				    "http://localhost:5000/translate");
	obs_data_set_default_string(
		s, "translate_cloud_body",
		"{\n\t\"text\":\"{{sentence}}\",\n\t\"target\":\"{{target_language}}\"\n}");
	obs_data_set_default_string(s, "translate_cloud_response_json_path", "translations.0.text");

	// Whisper parameters
	apply_whisper_params_defaults_on_settings(s);
}
