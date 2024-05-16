#pragma once

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include <string>
#include <vector>
#include <functional>

enum InputTokenizationStyle { INPUT_TOKENIZAION_M2M100 = 0, INPUT_TOKENIZAION_T5 };

struct translation_context {
	std::string local_model_folder_path;
	std::unique_ptr<sentencepiece::SentencePieceProcessor> processor;
	std::unique_ptr<ctranslate2::Translator> translator;
	std::unique_ptr<ctranslate2::TranslationOptions> options;
	std::function<std::vector<std::string>(const std::string &)> tokenizer;
	std::function<std::string(const std::vector<std::string> &)> detokenizer;
	std::vector<std::string> last_input_tokens;
	std::vector<std::string> last_translation_tokens;
	// Use the last translation as context for the next translation
	bool add_context;
	InputTokenizationStyle input_tokenization_style;
};

int build_translation_context(struct translation_context &translation_ctx);
void build_and_enable_translation(struct transcription_filter_data *gf,
				  const std::string &model_file_path);

int translate(struct translation_context &translation_ctx, const std::string &text,
	      const std::string &source_lang, const std::string &target_lang, std::string &result);

#define OBS_POLYGLOT_TRANSLATION_INIT_FAIL -1
#define OBS_POLYGLOT_TRANSLATION_INIT_SUCCESS 0
#define OBS_POLYGLOT_TRANSLATION_SUCCESS 0
#define OBS_POLYGLOT_TRANSLATION_FAIL -1
