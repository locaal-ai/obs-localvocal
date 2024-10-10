#include <util/profiler.hpp>

#include "resample-utils.h"

int get_data_from_buf_and_resample(transcription_filter_data *gf,
				   uint64_t &start_timestamp_offset_ns,
				   uint64_t &end_timestamp_offset_ns)
{
	uint32_t num_frames_from_infos = 0;

	{
		// scoped lock the buffer mutex
		std::lock_guard<std::mutex> lock(gf->whisper_buf_mutex);

		if (gf->input_buffers[0].size == 0) {
			return 1;
		}

		obs_log(gf->log_level,
			"segmentation: currently %lu bytes in the audio input buffer",
			gf->input_buffers[0].size);

		// max number of frames is 10 seconds worth of audio
		const size_t max_num_frames = gf->sample_rate * 10;

		// pop all infos from the info buffer and mark the beginning timestamp from the first
		// info as the beginning timestamp of the segment
		struct transcription_filter_audio_info info_from_buf = {0};
		const size_t size_of_audio_info = sizeof(transcription_filter_audio_info);
		while (gf->info_buffer.size >= size_of_audio_info) {
			circlebuf_pop_front(&gf->info_buffer, &info_from_buf, size_of_audio_info);
			num_frames_from_infos += info_from_buf.frames;
			if (start_timestamp_offset_ns == 0) {
				start_timestamp_offset_ns = info_from_buf.timestamp_offset_ns;
			}
			// Check if we're within the needed segment length
			if (num_frames_from_infos > max_num_frames) {
				// too big, push the last info into the buffer's front where it was
				num_frames_from_infos -= info_from_buf.frames;
				circlebuf_push_front(&gf->info_buffer, &info_from_buf,
						     size_of_audio_info);
				break;
			}
		}
		// calculate the end timestamp from the info plus the number of frames in the packet
		end_timestamp_offset_ns = info_from_buf.timestamp_offset_ns +
					  info_from_buf.frames * 1000000000 / gf->sample_rate;

		if (start_timestamp_offset_ns > end_timestamp_offset_ns) {
			// this may happen when the incoming media has a timestamp reset
			// in this case, we should figure out the start timestamp from the end timestamp
			// and the number of frames
			start_timestamp_offset_ns =
				end_timestamp_offset_ns -
				num_frames_from_infos * 1000000000 / gf->sample_rate;
		}

		for (size_t c = 0; c < gf->channels; c++) {
			// zero the rest of copy_buffers
			memset(gf->copy_buffers[c], 0, gf->frames * sizeof(float));
		}

		/* Pop from input circlebuf */
		for (size_t c = 0; c < gf->channels; c++) {
			// Push the new data to copy_buffers[c]
			circlebuf_pop_front(&gf->input_buffers[c], gf->copy_buffers[c],
					    num_frames_from_infos * sizeof(float));
		}
	}

	obs_log(gf->log_level, "found %d frames from info buffer.", num_frames_from_infos);
	gf->last_num_frames = num_frames_from_infos;

	{
		// resample to 16kHz
		float *resampled_16khz[MAX_PREPROC_CHANNELS];
		uint32_t resampled_16khz_frames;
		uint64_t ts_offset;
		{
			ProfileScope("resample");
			audio_resampler_resample(gf->resampler_to_whisper,
						 (uint8_t **)resampled_16khz,
						 &resampled_16khz_frames, &ts_offset,
						 (const uint8_t **)gf->copy_buffers,
						 (uint32_t)num_frames_from_infos);
		}

		circlebuf_push_back(&gf->resampled_buffer, resampled_16khz[0],
				    resampled_16khz_frames * sizeof(float));
		obs_log(gf->log_level,
			"resampled: %d channels, %d frames, %f ms, current size: %lu bytes",
			(int)gf->channels, (int)resampled_16khz_frames,
			(float)resampled_16khz_frames / WHISPER_SAMPLE_RATE * 1000.0f,
			gf->resampled_buffer.size);
	}

	return 0;
}
