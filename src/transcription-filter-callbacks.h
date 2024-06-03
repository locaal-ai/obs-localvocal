#ifndef TRANSCRIPTION_FILTER_CALLBACKS_H
#define TRANSCRIPTION_FILTER_CALLBACKS_H

#include <string>

#include "transcription-filter-data.h"
#include "whisper-utils/whisper-processing.h"

void send_caption_to_source(const std::string &target_source_name, const std::string &str_copy,
			    struct transcription_filter_data *gf);

void audio_chunk_callback(struct transcription_filter_data *gf, const float *pcm32f_data,
			  size_t frames, int vad_state, const DetectionResultWithText &result);

void set_text_callback(struct transcription_filter_data *gf,
		       const DetectionResultWithText &resultIn);

void recording_state_callback(enum obs_frontend_event event, void *data);

#endif /* TRANSCRIPTION_FILTER_CALLBACKS_H */
