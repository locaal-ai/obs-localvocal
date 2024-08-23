#ifndef VAD_PROCESSING_H
#define VAD_PROCESSING_H

enum VadState { VAD_STATE_WAS_ON = 0, VAD_STATE_WAS_OFF, VAD_STATE_IS_OFF, VAD_STATE_PARTIAL };

struct vad_state {
	bool vad_on;
	uint64_t start_ts_offest_ms;
	uint64_t end_ts_offset_ms;
	uint64_t last_partial_segment_end_ts;
};

vad_state vad_based_segmentation(transcription_filter_data *gf, vad_state last_vad_state);
void initialize_vad(transcription_filter_data *gf, const char *silero_vad_model_file);

#endif // VAD_PROCESSING_H
