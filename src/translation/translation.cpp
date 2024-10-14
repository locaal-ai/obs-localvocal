#include "translation.h"
#include "plugin-support.h"
#include "model-utils/model-find-utils.h"
#include "transcription-filter-data.h"
#include "language_codes.h"
#include "translation-language-utils.h"

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
	std::string local_spm_path = find_file_in_folder_by_regex_expression(
		local_model_path, "(sentencepiece|spm|spiece|source).*?\\.(model|spm)");
	std::string target_spm_path =
		find_file_in_folder_by_regex_expression(local_model_path, "target.*?\\.spm");

	try {
		obs_log(LOG_INFO, "Loading SPM from %s", local_spm_path.c_str());
		translation_ctx.processor.reset(new sentencepiece::SentencePieceProcessor());
		const auto status = translation_ctx.processor->Load(local_spm_path);
		if (!status.ok()) {
			obs_log(LOG_ERROR, "Failed to load SPM: %s", status.ToString().c_str());
			return OBS_POLYGLOT_TRANSLATION_INIT_FAIL;
		}

		if (!target_spm_path.empty()) {
			obs_log(LOG_INFO, "Loading target SPM from %s", target_spm_path.c_str());
			translation_ctx.target_processor.reset(
				new sentencepiece::SentencePieceProcessor());
			const auto target_status =
				translation_ctx.target_processor->Load(target_spm_path);
			if (!target_status.ok()) {
				obs_log(LOG_ERROR, "Failed to load target SPM: %s",
					target_status.ToString().c_str());
				return OBS_POLYGLOT_TRANSLATION_INIT_FAIL;
			}
		} else {
			obs_log(LOG_INFO, "Target SPM not found, using source SPM for target");
			translation_ctx.target_processor.release();
		}

		translation_ctx.tokenizer = [&translation_ctx](const std::string &text) {
			std::vector<std::string> tokens;
			translation_ctx.processor->Encode(text, &tokens);
			return tokens;
		};
		translation_ctx.detokenizer =
			[&translation_ctx](const std::vector<std::string> &tokens) {
				std::string text;
				if (translation_ctx.target_processor) {
					translation_ctx.target_processor->Decode(tokens, &text);
				} else {
					translation_ctx.processor->Decode(tokens, &text);
				}
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
		translation_ctx.options->max_decoding_length = 64;
		translation_ctx.options->repetition_penalty = 2.0f;
		translation_ctx.options->no_repeat_ngram_size = 1;
		translation_ctx.options->max_input_length = 64;
		translation_ctx.options->sampling_temperature = 0.1f;
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
		std::vector<ctranslate2::TranslationResult> results;
		std::vector<std::string> target_prefix;

		if (translation_ctx.input_tokenization_style == INPUT_TOKENIZAION_M2M100) {
			// set input tokens
			std::vector<std::string> input_tokens = {source_lang, "<s>"};
			if (translation_ctx.add_context > 0 &&
			    translation_ctx.last_input_tokens.size() > 0) {
				obs_log(translation_ctx.log_level,
					"Adding last input tokens to input tokens, size: %d",
					(int)translation_ctx.last_input_tokens.size());
				// add the last input tokens sentences to the input tokens
				for (const auto &tokens : translation_ctx.last_input_tokens) {
					input_tokens.insert(input_tokens.end(), tokens.begin(),
							    tokens.end());
				}
			}
			std::vector<std::string> new_input_tokens = translation_ctx.tokenizer(text);
			input_tokens.insert(input_tokens.end(), new_input_tokens.begin(),
					    new_input_tokens.end());
			input_tokens.push_back("</s>");

			// log the input tokens
			std::string input_tokens_str;
			for (const auto &token : input_tokens) {
				input_tokens_str += token + ", ";
			}
			obs_log(translation_ctx.log_level, "Input tokens: %s",
				input_tokens_str.c_str());

			if (translation_ctx.add_context > 0) {
				translation_ctx.last_input_tokens.push_back(new_input_tokens);
				obs_log(translation_ctx.log_level,
					"Adding last input context. Last input tokens deque size: %d",
					(int)translation_ctx.last_input_tokens.size());
				// remove the oldest input tokens
				while (translation_ctx.last_input_tokens.size() >
				       (size_t)translation_ctx.add_context) {
					obs_log(translation_ctx.log_level,
						"Removing oldest input tokens context, size: %d",
						(int)translation_ctx.last_input_tokens.size());
					translation_ctx.last_input_tokens.pop_front();
				}
			} else {
				translation_ctx.last_input_tokens.clear();
			}

			const std::vector<std::vector<std::string>> batch = {input_tokens};

			// get target prefix
			target_prefix = {target_lang};
			// add the last translation tokens to the target prefix
			if (translation_ctx.add_context > 0 &&
			    translation_ctx.last_translation_tokens.size() > 0) {
				obs_log(translation_ctx.log_level,
					"Adding last translation tokens to target prefix, size: %d",
					(int)translation_ctx.last_translation_tokens.size());
				for (const auto &tokens : translation_ctx.last_translation_tokens) {
					target_prefix.insert(target_prefix.end(), tokens.begin(),
							     tokens.end());
				}
			}

			// log the target prefix
			std::string target_prefix_str;
			for (const auto &token : target_prefix) {
				target_prefix_str += token + ",";
			}
			obs_log(translation_ctx.log_level, "Target prefix: %s",
				target_prefix_str.c_str());

			const std::vector<std::vector<std::string>> target_prefix_batch = {
				target_prefix};
			results = translation_ctx.translator->translate_batch(
				batch, target_prefix_batch, *translation_ctx.options);
		} else {
			// set input tokens
			std::vector<std::string> input_tokens = {};
			std::vector<std::string> new_input_tokens = translation_ctx.tokenizer(
				"<2" + language_codes_to_whisper[target_lang] + "> " + text);
			input_tokens.insert(input_tokens.end(), new_input_tokens.begin(),
					    new_input_tokens.end());
			const std::vector<std::vector<std::string>> batch = {input_tokens};

			results = translation_ctx.translator->translate_batch(
				batch, {}, *translation_ctx.options);
		}

		const auto &tokens_result = results[0].output();
		// take the tokens from the target_prefix length to the end
		std::vector<std::string> translation_tokens(
			tokens_result.begin() + target_prefix.size(), tokens_result.end());

		// log the translation tokens
		std::string translation_tokens_str;
		for (const auto &token : translation_tokens) {
			translation_tokens_str += token + ", ";
		}
		obs_log(translation_ctx.log_level, "Translation tokens: %s",
			translation_tokens_str.c_str());

		if (translation_ctx.add_context > 0) {
			// save the translation tokens
			translation_ctx.last_translation_tokens.push_back(translation_tokens);
			// remove the oldest translation tokens
			while (translation_ctx.last_translation_tokens.size() >
			       (size_t)translation_ctx.add_context) {
				obs_log(translation_ctx.log_level,
					"Removing oldest translation tokens context, size: %d",
					(int)translation_ctx.last_translation_tokens.size());
				translation_ctx.last_translation_tokens.pop_front();
			}
			obs_log(translation_ctx.log_level, "Last translation tokens deque size: %d",
				(int)translation_ctx.last_translation_tokens.size());
		} else {
			translation_ctx.last_translation_tokens.clear();
		}

		// detokenize
		const std::string result_ = translation_ctx.detokenizer(translation_tokens);
		if (translation_ctx.remove_punctuation_from_start) {
			result = remove_start_punctuation(result_);
		} else {
			result = result_;
		}
	} catch (std::exception &e) {
		obs_log(LOG_ERROR, "Error: %s", e.what());
		return OBS_POLYGLOT_TRANSLATION_FAIL;
	}
	return OBS_POLYGLOT_TRANSLATION_SUCCESS;
}
