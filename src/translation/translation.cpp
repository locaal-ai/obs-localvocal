#include "translation.h"
#include "plugin-support.h"

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include <obs-module.h>
#include <regex>

int build_translation_context(struct translation_context &translation_ctx,
			      const std::string &local_spm_path,
			      const std::string &local_model_path)
{
	obs_log(LOG_INFO, "Building translation context...");
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
		obs_log(LOG_INFO, "Using CUDA");
#else
		ctranslate2::Device device = ctranslate2::Device::CPU;
		obs_log(LOG_INFO, "Using CPU");
#endif

		translation_ctx.translator.reset(new ctranslate2::Translator(
			local_model_path, device, ctranslate2::ComputeType::AUTO));
		obs_log(LOG_INFO, "CT2 Model loaded");

		translation_ctx.options.reset(new ctranslate2::TranslationOptions);
		translation_ctx.options->beam_size = 1;
		translation_ctx.options->max_decoding_length = 40;
		translation_ctx.options->use_vmap = true;
		translation_ctx.options->return_scores = false;
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
		// get tokens
		std::vector<std::string> tokens = translation_ctx.tokenizer(text);
		tokens.insert(tokens.begin(), "<s>");
		tokens.insert(tokens.begin(), source_lang);
		tokens.push_back("</s>");

		const std::vector<std::vector<std::string>> batch = {tokens};

		const std::vector<std::vector<std::string>> target_prefix = {{target_lang}};
		const std::vector<ctranslate2::TranslationResult> results =
			translation_ctx.translator->translate_batch(batch, target_prefix,
								    *translation_ctx.options);

		// detokenize starting with the 2nd token
		const auto &tokens_result = results[0].output();
		result = translation_ctx.detokenizer(
			std::vector<std::string>(tokens_result.begin() + 1, tokens_result.end()));
	} catch (std::exception &e) {
		obs_log(LOG_ERROR, "Error: %s", e.what());
		return OBS_POLYGLOT_TRANSLATION_FAIL;
	}
	return OBS_POLYGLOT_TRANSLATION_SUCCESS;
}
