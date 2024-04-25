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

const char *const PLUGIN_INFO_TEMPLATE =
	"<a href=\"https://github.com/occ-ai/obs-localvocal/\">LocalVocal</a> (%1) by "
	"<a href=\"https://github.com/occ-ai\">OCC AI</a> ❤️ "
	"<a href=\"https://www.patreon.com/RoyShilkrot\">Support & Follow</a>";

const char *const SUPPRESS_SENTENCES_DEFAULT = "Thank you for watching\nThank you";

#ifdef __cplusplus
}
#endif
