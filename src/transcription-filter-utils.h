#ifndef TRANSCRIPTION_FILTER_UTILS_H
#define TRANSCRIPTION_FILTER_UTILS_H

#include <media-io/audio-io.h>
#include <obs.h>

// Convert channels number to a speaker layout
inline enum speaker_layout convert_speaker_layout(uint8_t channels)
{
	switch (channels) {
	case 0:
		return SPEAKERS_UNKNOWN;
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_UNKNOWN;
	}
}

void create_obs_text_source();

bool add_sources_to_list(void *list_property, obs_source_t *source);

#endif // TRANSCRIPTION_FILTER_UTILS_H
