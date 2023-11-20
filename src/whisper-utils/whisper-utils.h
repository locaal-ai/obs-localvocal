#ifndef WHISPER_UTILS_H
#define WHISPER_UTILS_H

#include "transcription-filter-data.h"

#include <obs.h>

#include <string>

void update_whsiper_model_path(struct transcription_filter_data *gf, obs_data_t *s);
void shutdown_whisper_thread(struct transcription_filter_data *gf);
void start_whisper_thread_with_path(struct transcription_filter_data *gf, const std::string &path);

#endif /* WHISPER_UTILS_H */
