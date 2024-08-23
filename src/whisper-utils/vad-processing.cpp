
#include <util/profiler.hpp>

#include "transcription-filter-data.h"

#include "vad-processing.h"

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

vad_state vad_based_segmentation(transcription_filter_data *gf, vad_state last_vad_state)
{
	uint32_t num_frames_from_infos = 0;
	uint64_t start_timestamp_offset_ns = 0;
	uint64_t end_timestamp_offset_ns = 0;

	{
		// scoped lock the buffer mutex
		std::lock_guard<std::mutex> lock(gf->whisper_buf_mutex);

		if (gf->input_buffers[0].size == 0) {
			return last_vad_state;
		}

		obs_log(gf->log_level,
			"vad based segmentation. currently %lu bytes in the audio input buffer",
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

		obs_log(gf->log_level, "resampled: %d channels, %d frames, %f ms",
			(int)gf->channels, (int)resampled_16khz_frames,
			(float)resampled_16khz_frames / WHISPER_SAMPLE_RATE * 1000.0f);
		circlebuf_push_back(&gf->resampled_buffer, resampled_16khz[0],
				    resampled_16khz_frames * sizeof(float));
	}

	const size_t vad_window_size_samples = gf->vad->get_window_size_samples() * sizeof(float);
	const size_t min_vad_buffer_size = vad_window_size_samples * 8;
	if (gf->resampled_buffer.size < min_vad_buffer_size)
		return last_vad_state;

	size_t vad_num_windows = gf->resampled_buffer.size / vad_window_size_samples;

	std::vector<float> vad_input;
	vad_input.resize(vad_num_windows * gf->vad->get_window_size_samples());
	circlebuf_pop_front(&gf->resampled_buffer, vad_input.data(),
			    vad_input.size() * sizeof(float));

	obs_log(gf->log_level, "sending %d frames to vad, %d windows, reset state? %s",
		vad_input.size(), vad_num_windows, (!last_vad_state.vad_on) ? "yes" : "no");
	{
		ProfileScope("vad->process");
		gf->vad->process(vad_input, !last_vad_state.vad_on);
	}

	const uint64_t start_ts_offset_ms = start_timestamp_offset_ns / 1000000;
	const uint64_t end_ts_offset_ms = end_timestamp_offset_ns / 1000000;

	vad_state current_vad_state = {false, start_ts_offset_ms, end_ts_offset_ms,
				       last_vad_state.last_partial_segment_end_ts};

	std::vector<timestamp_t> stamps = gf->vad->get_speech_timestamps();
	if (stamps.size() == 0) {
		obs_log(gf->log_level, "VAD detected no speech in %u frames", vad_input.size());
		if (last_vad_state.vad_on) {
			obs_log(gf->log_level, "Last VAD was ON: segment end -> send to inference");
			run_inference_and_callbacks(gf, last_vad_state.start_ts_offest_ms,
						    last_vad_state.end_ts_offset_ms,
						    VAD_STATE_WAS_ON);
			current_vad_state.last_partial_segment_end_ts = 0;
		}

		if (gf->enable_audio_chunks_callback) {
			audio_chunk_callback(gf, vad_input.data(), vad_input.size(),
					     VAD_STATE_IS_OFF,
					     {DETECTION_RESULT_SILENCE,
					      "[silence]",
					      current_vad_state.start_ts_offest_ms,
					      current_vad_state.end_ts_offset_ms,
					      {}});
		}

		return current_vad_state;
	}

	// process vad segments
	for (size_t i = 0; i < stamps.size(); i++) {
		int start_frame = stamps[i].start;
		if (i > 0) {
			// if this is not the first segment, start from the end of the previous segment
			start_frame = stamps[i - 1].end;
		} else {
			// take at least 100ms of audio before the first speech segment, if available
			start_frame = std::max(0, start_frame - WHISPER_SAMPLE_RATE / 10);
		}

		int end_frame = stamps[i].end;
		// if (i == stamps.size() - 1 && stamps[i].end < (int)vad_input.size()) {
		// 	// take at least 100ms of audio after the last speech segment, if available
		// 	end_frame = std::min(end_frame + WHISPER_SAMPLE_RATE / 10,
		// 			     (int)vad_input.size());
		// }

		const int number_of_frames = end_frame - start_frame;

		// push the data into gf-whisper_buffer
		circlebuf_push_back(&gf->whisper_buffer, vad_input.data() + start_frame,
				    number_of_frames * sizeof(float));

		obs_log(gf->log_level,
			"VAD segment %d/%d. pushed %d to %d (%d frames / %lu ms). current size: %lu bytes / %lu frames / %lu ms",
			i, (stamps.size() - 1), start_frame, end_frame, number_of_frames,
			number_of_frames * 1000 / WHISPER_SAMPLE_RATE, gf->whisper_buffer.size,
			gf->whisper_buffer.size / sizeof(float),
			gf->whisper_buffer.size / sizeof(float) * 1000 / WHISPER_SAMPLE_RATE);

		// segment "end" is in the middle of the buffer, send it to inference
		if (stamps[i].end < (int)vad_input.size()) {
			// new "ending" segment (not up to the end of the buffer)
			obs_log(gf->log_level, "VAD segment end -> send to inference");
			// find the end timestamp of the segment
			const uint64_t segment_end_ts =
				start_ts_offset_ms + end_frame * 1000 / WHISPER_SAMPLE_RATE;
			run_inference_and_callbacks(
				gf, last_vad_state.start_ts_offest_ms, segment_end_ts,
				last_vad_state.vad_on ? VAD_STATE_WAS_ON : VAD_STATE_WAS_OFF);
			current_vad_state.vad_on = false;
			current_vad_state.start_ts_offest_ms = current_vad_state.end_ts_offset_ms;
			current_vad_state.end_ts_offset_ms = 0;
			current_vad_state.last_partial_segment_end_ts = 0;
			last_vad_state = current_vad_state;
			continue;
		}

		// end not reached - speech is ongoing
		current_vad_state.vad_on = true;
		if (last_vad_state.vad_on) {
			obs_log(gf->log_level,
				"last vad state was: ON, start ts: %llu, end ts: %llu",
				last_vad_state.start_ts_offest_ms, last_vad_state.end_ts_offset_ms);
			current_vad_state.start_ts_offest_ms = last_vad_state.start_ts_offest_ms;
		} else {
			obs_log(gf->log_level,
				"last vad state was: OFF, start ts: %llu, end ts: %llu. start_ts_offset_ms: %llu, start_frame: %d",
				last_vad_state.start_ts_offest_ms, last_vad_state.end_ts_offset_ms,
				start_ts_offset_ms, start_frame);
			current_vad_state.start_ts_offest_ms =
				start_ts_offset_ms + start_frame * 1000 / WHISPER_SAMPLE_RATE;
		}
		current_vad_state.end_ts_offset_ms =
			start_ts_offset_ms + end_frame * 1000 / WHISPER_SAMPLE_RATE;
		obs_log(gf->log_level,
			"end not reached. vad state: ON, start ts: %llu, end ts: %llu",
			current_vad_state.start_ts_offest_ms, current_vad_state.end_ts_offset_ms);

		last_vad_state = current_vad_state;

		// if partial transcription is enabled, check if we should send a partial segment
		if (!gf->partial_transcription) {
			continue;
		}

		// current length of audio in buffer
		const uint64_t current_length_ms =
			(current_vad_state.end_ts_offset_ms > 0
				 ? current_vad_state.end_ts_offset_ms
				 : current_vad_state.start_ts_offest_ms) -
			(current_vad_state.last_partial_segment_end_ts > 0
				 ? current_vad_state.last_partial_segment_end_ts
				 : current_vad_state.start_ts_offest_ms);
		obs_log(gf->log_level, "current buffer length after last partial (%lu): %lu ms",
			current_vad_state.last_partial_segment_end_ts, current_length_ms);

		if (current_length_ms > (uint64_t)gf->partial_latency) {
			current_vad_state.last_partial_segment_end_ts =
				current_vad_state.end_ts_offset_ms;
			// send partial segment to inference
			obs_log(gf->log_level, "Partial segment -> send to inference");
			run_inference_and_callbacks(gf, current_vad_state.start_ts_offest_ms,
						    current_vad_state.end_ts_offset_ms,
						    VAD_STATE_PARTIAL);
		}
	}

	return current_vad_state;
}

void initialize_vad(transcription_filter_data *gf, const char *silero_vad_model_file)
{
	// initialize Silero VAD
#ifdef _WIN32
	// convert mbstring to wstring
	int count = MultiByteToWideChar(CP_UTF8, 0, silero_vad_model_file,
					strlen(silero_vad_model_file), NULL, 0);
	std::wstring silero_vad_model_path(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, silero_vad_model_file, strlen(silero_vad_model_file),
			    &silero_vad_model_path[0], count);
	obs_log(gf->log_level, "Create silero VAD: %S", silero_vad_model_path.c_str());
#else
	std::string silero_vad_model_path = silero_vad_model_file;
	obs_log(gf->log_level, "Create silero VAD: %s", silero_vad_model_path.c_str());
#endif
	// roughly following https://github.com/SYSTRAN/faster-whisper/blob/master/faster_whisper/vad.py
	// for silero vad parameters
	gf->vad.reset(new VadIterator(silero_vad_model_path, WHISPER_SAMPLE_RATE, 32, 0.5f, 200,
				      100, 100));
}
