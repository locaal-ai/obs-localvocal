#ifndef WHISPER_MODEL_UTILS_H
#define WHISPER_MODEL_UTILS_H

#include <obs.h>

#include "transcription-filter-data.h"

void update_whisper_model(struct transcription_filter_data *gf, obs_data_t *s);

#endif // WHISPER_MODEL_UTILS_H
