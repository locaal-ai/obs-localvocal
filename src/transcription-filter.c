#include "transcription-filter.h"

struct obs_source_info transcription_filter_info = {
	.id = "transcription_filter_audio_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = transcription_filter_name,
	.create = transcription_filter_create,
	.destroy = transcription_filter_destroy,
	.get_defaults = transcription_filter_defaults,
	.get_properties = transcription_filter_properties,
	.update = transcription_filter_update,
	.activate = transcription_filter_activate,
	.deactivate = transcription_filter_deactivate,
	.filter_audio = transcription_filter_filter_audio,
	.filter_remove = transcription_filter_remove,
};
