#ifndef STENOGRAPHER_UTIL_H
#define STENOGRAPHER_UTIL_H

struct transcription_filter_data;
struct obs_audio_data;

struct obs_audio_data *stenographer_simple_delay(transcription_filter_data *gf,
						 struct obs_audio_data *audio);

#endif /* STENOGRAPHER_UTIL_H */