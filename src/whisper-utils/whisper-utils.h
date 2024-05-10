#ifndef WHISPER_UTILS_H
#define WHISPER_UTILS_H

#include "transcription-filter-data.h"

#include <string>

void shutdown_whisper_thread(struct transcription_filter_data *gf);
void start_whisper_thread_with_path(struct transcription_filter_data *gf, const std::string &path,
				    const char *silero_vad_model_file);

std::pair<int, int> findStartOfOverlap(const std::vector<whisper_token_data> &seq1,
				       const std::vector<whisper_token_data> &seq2);
std::vector<whisper_token_data> reconstructSentence(const std::vector<whisper_token_data> &seq1,
						    const std::vector<whisper_token_data> &seq2);

#endif /* WHISPER_UTILS_H */
