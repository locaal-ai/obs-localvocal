#pragma once

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include <string>
#include <vector>
#include <functional>

struct translation_context {
	std::unique_ptr<sentencepiece::SentencePieceProcessor> processor;
	std::unique_ptr<ctranslate2::Translator> translator;
	std::unique_ptr<ctranslate2::TranslationOptions> options;
	std::function<std::vector<std::string>(const std::string &)> tokenizer;
	std::function<std::string(const std::vector<std::string> &)> detokenizer;
};

int build_translation_context(struct translation_context &translation_ctx,
			      const std::string &local_spm_path,
			      const std::string &local_model_path);

int translate(struct translation_context &translation_ctx, const std::string &text,
	      const std::string &source_lang, const std::string &target_lang, std::string &result);

#define OBS_POLYGLOT_TRANSLATION_INIT_FAIL -1
#define OBS_POLYGLOT_TRANSLATION_INIT_SUCCESS 0
#define OBS_POLYGLOT_TRANSLATION_SUCCESS 0
#define OBS_POLYGLOT_TRANSLATION_FAIL -1
