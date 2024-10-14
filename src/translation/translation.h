#ifndef TRANSLATION_H
#define TRANSLATION_H

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <memory>

enum InputTokenizationStyle { INPUT_TOKENIZAION_M2M100 = 0, INPUT_TOKENIZAION_T5 };

namespace ctranslate2 {
class Translator;
class TranslationOptions;
} // namespace ctranslate2

namespace sentencepiece {
class SentencePieceProcessor;
} // namespace sentencepiece

struct translation_context {
	std::string local_model_folder_path;
	std::unique_ptr<sentencepiece::SentencePieceProcessor> processor;
	std::unique_ptr<sentencepiece::SentencePieceProcessor> target_processor;
	std::unique_ptr<ctranslate2::Translator> translator;
	std::unique_ptr<ctranslate2::TranslationOptions> options;
	std::function<std::vector<std::string>(const std::string &)> tokenizer;
	std::function<std::string(const std::vector<std::string> &)> detokenizer;
	std::deque<std::vector<std::string>> last_input_tokens;
	std::deque<std::vector<std::string>> last_translation_tokens;
	// How many sentences to use as context for the next translation
	int add_context;
	InputTokenizationStyle input_tokenization_style;
	bool remove_punctuation_from_start;
	int log_level = 400;
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

#endif // TRANSLATION_H
