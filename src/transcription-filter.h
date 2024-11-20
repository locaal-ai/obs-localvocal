#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MT_ obs_module_text

void transcription_filter_activate(void *data);
void *transcription_filter_create(obs_data_t *settings, obs_source_t *filter);
void transcription_filter_update(void *data, obs_data_t *s);
void transcription_filter_destroy(void *data);
const char *transcription_filter_name(void *unused);
struct obs_audio_data *transcription_filter_filter_audio(void *data, struct obs_audio_data *audio);
void transcription_filter_deactivate(void *data);
void transcription_filter_defaults(obs_data_t *s);
obs_properties_t *transcription_filter_properties(void *data);
void transcription_filter_remove(void *data, obs_source_t *source);
void transcription_filter_show(void *data);
void transcription_filter_hide(void *data);

const char *const PLUGIN_INFO_TEMPLATE =
	"<a href=\"https://github.com/locaal-ai/obs-localvocal/\">LocalVocal</a> (%1) by "
	"<a href=\"https://github.com/locaal-ai\">Locaal AI</a> ❤️ "
	"<a href=\"https://locaal.ai\">Support & Follow</a>";

const char *const SUPPRESS_SENTENCES_DEFAULT =
	"Thank you for watching\nPlease like and subscribe\n"
	"Check out my other videos\nFollow me on social media\n"
	"Please consider supporting me";

#ifdef __cplusplus
}
#endif
