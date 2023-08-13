#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

void transcription_filter_activate(void *data);
void *transcription_filter_create(obs_data_t *settings, obs_source_t *filter);
void transcription_filter_update(void *data, obs_data_t *s);
void transcription_filter_destroy(void *data);
const char *transcription_filter_name(void *unused);
struct obs_audio_data *transcription_filter_filter_audio(void *data, struct obs_audio_data *audio);
void transcription_filter_deactivate(void *data);
void transcription_filter_defaults(obs_data_t *s);
obs_properties_t *transcription_filter_properties(void *data);

#ifdef __cplusplus
}
#endif
