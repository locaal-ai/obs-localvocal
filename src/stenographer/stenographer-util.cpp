
#include "stenographer-util.h"
#include "transcription-filter-data.h"
#include "transcription-utils.h"

#include <obs.h>

#include <cstring>
#include <vector>

/**
 * @brief Applies a simple delay to the audio data for stenographer mode.
 *
 * This function stores the incoming audio data in a buffer and processes it after a specified delay.
 * The delayed audio data is then emitted, replacing the original audio data in the buffer.
 * If the buffer does not yet contain enough data to satisfy the delay, the audio buffer is filled with silence.
 *
 * @param gf Pointer to the transcription filter data structure containing the delay buffer and configuration.
 * @param audio Pointer to the audio data structure containing the audio frames to be processed.
 * @return Pointer to the processed audio data structure with the applied delay.
 */
struct obs_audio_data *stenographer_simple_delay(transcription_filter_data *gf,
						 struct obs_audio_data *audio)
{
	// Stenographer mode - apply delay.
	// Store the audio data in a buffer and process it after the delay.
	// push the data to the back of gf->stenographer_delay_buffer
	for (size_t c = 0; c < gf->channels; c++) {
		// take a audio->frames * sizeof(float) bytes chunk from audio->data[c] and push it
		// to the back of the buffer as a float
		std::vector<float> audio_data_chunk((float *)audio->data[c],
						    ((float *)audio->data[c]) + audio->frames);
		gf->stenographer_delay_buffers[c].insert(gf->stenographer_delay_buffers[c].end(),
							 audio_data_chunk.begin(),
							 audio_data_chunk.end());
	}

	// If the buffer is larger than the delay, emit the oldest data
	// Take from the buffer as much as requested by the incoming audio data
	size_t delay_frames =
		(size_t)((float)gf->sample_rate * (float)gf->stenographer_delay_ms / 1000.0f) +
		audio->frames;

	if (gf->stenographer_delay_buffers[0].size() >= delay_frames) {
		// Replace data on the audio buffer with the delayed data
		for (size_t c = 0; c < gf->channels; c++) {
			// take exatcly audio->frames from the buffer
			std::vector<float> audio_data(gf->stenographer_delay_buffers[c].begin(),
						      gf->stenographer_delay_buffers[c].begin() +
							      audio->frames);
			// remove the oldest buffers from the delay buffer
			gf->stenographer_delay_buffers[c].erase(
				gf->stenographer_delay_buffers[c].begin(),
				gf->stenographer_delay_buffers[c].begin() + audio->frames);

			// replace the data on the audio buffer with the delayed data
			memcpy(audio->data[c], audio_data.data(),
			       audio_data.size() * sizeof(float));
		}
	} else {
		// Fill the audio buffer with silence
		for (size_t c = 0; c < gf->channels; c++) {
			memset(audio->data[c], 0, audio->frames * sizeof(float));
		}
	}
	return audio;
}
