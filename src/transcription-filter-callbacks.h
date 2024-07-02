#ifndef TRANSCRIPTION_FILTER_CALLBACKS_H
#define TRANSCRIPTION_FILTER_CALLBACKS_H

#include <string>

#include "transcription-filter-data.h"
#include "whisper-utils/whisper-processing.h"

void send_caption_to_source(const std::string &target_source_name, const std::string &str_copy,
			    struct transcription_filter_data *gf);
std::string send_sentence_to_translation(const std::string &sentence,
					 struct transcription_filter_data *gf);

void audio_chunk_callback(struct transcription_filter_data *gf, const float *pcm32f_data,
			  size_t frames, int vad_state, const DetectionResultWithText &result);

void set_text_callback(struct transcription_filter_data *gf,
		       const DetectionResultWithText &resultIn);

void recording_state_callback(enum obs_frontend_event event, void *data);

void media_play_callback(void *data_, calldata_t *cd);
void media_started_callback(void *data_, calldata_t *cd);
void media_pause_callback(void *data_, calldata_t *cd);
void media_restart_callback(void *data_, calldata_t *cd);
void media_stopped_callback(void *data_, calldata_t *cd);
void enable_callback(void *data_, calldata_t *cd);

#endif /* TRANSCRIPTION_FILTER_CALLBACKS_H */
