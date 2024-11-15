#ifndef VAD_PROCESSING_H
#define VAD_PROCESSING_H

/**
 * @file vad-processing.h
 * @brief Header file for Voice Activity Detection (VAD) processing utilities.
 *
 * This file contains the declarations of enums, structs, and functions used for
 * VAD processing in the transcription filter.
 */

/**
 * @enum VadState
 * @brief Enumeration of possible VAD states.
 *
 * - VAD_STATE_WAS_ON: VAD was previously on.
 * - VAD_STATE_WAS_OFF: VAD was previously off.
 * - VAD_STATE_IS_OFF: VAD is currently off.
 * - VAD_STATE_PARTIAL: VAD is in a partial state.
 */
enum VadState { VAD_STATE_WAS_ON = 0, VAD_STATE_WAS_OFF, VAD_STATE_IS_OFF, VAD_STATE_PARTIAL };

/**
 * @enum VadMode
 * @brief Enumeration of possible VAD modes.
 *
 * - VAD_MODE_ACTIVE: VAD is actively processing.
 * - VAD_MODE_HYBRID: VAD is in hybrid mode.
 * - VAD_MODE_DISABLED: VAD is disabled.
 */
enum VadMode { VAD_MODE_ACTIVE = 0, VAD_MODE_HYBRID, VAD_MODE_DISABLED };

/**
 * @struct vad_state
 * @brief Structure representing the state of VAD.
 *
 * @var vad_state::vad_on
 * Indicates whether VAD is currently on.
 * @var vad_state::start_ts_offest_ms
 * Timestamp offset in milliseconds when VAD started.
 * @var vad_state::end_ts_offset_ms
 * Timestamp offset in milliseconds when VAD ended.
 * @var vad_state::last_partial_segment_end_ts
 * Timestamp of the end of the last partial segment.
 */
struct vad_state {
	bool vad_on;
	uint64_t start_ts_offest_ms;
	uint64_t end_ts_offset_ms;
	uint64_t last_partial_segment_end_ts;
};

vad_state vad_disabled_segmentation(transcription_filter_data *gf, vad_state last_vad_state);
vad_state vad_based_segmentation(transcription_filter_data *gf, vad_state last_vad_state);
vad_state hybrid_vad_segmentation(transcription_filter_data *gf, vad_state last_vad_state);
void initialize_vad(transcription_filter_data *gf, const char *silero_vad_model_file);

#endif // VAD_PROCESSING_H
