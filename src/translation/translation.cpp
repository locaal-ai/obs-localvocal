#include "translation.h"
#include "plugin-support.h"
#include "model-utils/model-find-utils.h"
#include "transcription-filter-data.h"

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include <obs-module.h>
#include <regex>

void build_and_enable_translation(struct transcription_filter_data *gf,
				  const std::string &model_file_path)
{
	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);

	gf->translation_ctx.local_model_folder_path = model_file_path;
	if (build_translation_context(gf->translation_ctx) ==
	    OBS_POLYGLOT_TRANSLATION_INIT_SUCCESS) {
		obs_log(LOG_INFO, "Enable translation");
		gf->translate = true;
	} else {
		obs_log(LOG_ERROR, "Failed to load CT2 model");
		gf->translate = false;
	}
}

int build_translation_context(struct translation_context &translation_ctx)
{
	std::string local_model_path = translation_ctx.local_model_folder_path;
	obs_log(LOG_INFO, "Building translation context from '%s'...", local_model_path.c_str());
	// find the SPM file in the model folder
	std::string local_spm_path =
		find_file_in_folder_by_name(local_model_path, "sentencepiece.bpe.model");

	try {
		obs_log(LOG_INFO, "Loading SPM from %s", local_spm_path.c_str());
		translation_ctx.processor.reset(new sentencepiece::SentencePieceProcessor());
		const auto status = translation_ctx.processor->Load(local_spm_path);
		if (!status.ok()) {
			obs_log(LOG_ERROR, "Failed to load SPM: %s", status.ToString().c_str());
			return OBS_POLYGLOT_TRANSLATION_INIT_FAIL;
		}

		translation_ctx.tokenizer = [&translation_ctx](const std::string &text) {
			std::vector<std::string> tokens;
			translation_ctx.processor->Encode(text, &tokens);
			return tokens;
		};
		translation_ctx.detokenizer =
			[&translation_ctx](const std::vector<std::string> &tokens) {
				std::string text;
				translation_ctx.processor->Decode(tokens, &text);
				return std::regex_replace(text, std::regex("<unk>"), "UNK");
			};

		obs_log(LOG_INFO, "Loading CT2 model from %s", local_model_path.c_str());

#ifdef POLYGLOT_WITH_CUDA
		ctranslate2::Device device = ctranslate2::Device::CUDA;
		obs_log(LOG_INFO, "CT2 Using CUDA");
#else
		ctranslate2::Device device = ctranslate2::Device::CPU;
		obs_log(LOG_INFO, "CT2 Using CPU");
#endif

		translation_ctx.translator.reset(new ctranslate2::Translator(
			local_model_path, device, ctranslate2::ComputeType::AUTO));
		obs_log(LOG_INFO, "CT2 Model loaded");

		translation_ctx.options.reset(new ctranslate2::TranslationOptions);
		translation_ctx.options->beam_size = 1;
		translation_ctx.options->max_decoding_length = 40;
		translation_ctx.options->use_vmap = true;
		translation_ctx.options->return_scores = false;
		translation_ctx.options->repetition_penalty = 1.1f;
		translation_ctx.options->no_repeat_ngram_size = 2;
	} catch (std::exception &e) {
		obs_log(LOG_ERROR, "Failed to load CT2 model: %s", e.what());
		return OBS_POLYGLOT_TRANSLATION_INIT_FAIL;
	}
	return OBS_POLYGLOT_TRANSLATION_INIT_SUCCESS;
}

int translate(struct translation_context &translation_ctx, const std::string &text,
	      const std::string &source_lang, const std::string &target_lang, std::string &result)
{
	try {
		// set input tokens
		std::vector<std::string> input_tokens = {source_lang, "<s>"};
		if (translation_ctx.add_context && translation_ctx.last_input_tokens.size() > 0) {
			input_tokens.insert(input_tokens.end(),
					    translation_ctx.last_input_tokens.begin(),
					    translation_ctx.last_input_tokens.end());
		}
		std::vector<std::string> new_input_tokens = translation_ctx.tokenizer(text);
		input_tokens.insert(input_tokens.end(), new_input_tokens.begin(),
				    new_input_tokens.end());
		input_tokens.push_back("</s>");

		translation_ctx.last_input_tokens = new_input_tokens;

		const std::vector<std::vector<std::string>> batch = {input_tokens};

		// get target prefix
		std::vector<std::string> target_prefix = {target_lang};
		if (translation_ctx.add_context &&
		    translation_ctx.last_translation_tokens.size() > 0) {
			target_prefix.insert(target_prefix.end(),
					     translation_ctx.last_translation_tokens.begin(),
					     translation_ctx.last_translation_tokens.end());
		}

		const std::vector<std::vector<std::string>> target_prefix_batch = {target_prefix};
		const std::vector<ctranslate2::TranslationResult> results =
			translation_ctx.translator->translate_batch(batch, target_prefix_batch,
								    *translation_ctx.options);

		const auto &tokens_result = results[0].output();
		// take the tokens from the target_prefix length to the end
		std::vector<std::string> translation_tokens(
			tokens_result.begin() + target_prefix.size(), tokens_result.end());

		translation_ctx.last_translation_tokens = translation_tokens;
		// detokenize
		result = translation_ctx.detokenizer(translation_tokens);
	} catch (std::exception &e) {
		obs_log(LOG_ERROR, "Error: %s", e.what());
		return OBS_POLYGLOT_TRANSLATION_FAIL;
	}
	return OBS_POLYGLOT_TRANSLATION_SUCCESS;
}
