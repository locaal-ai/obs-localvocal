#ifndef TRANSCRIPTION_FILTER_DATA_H
#define TRANSCRIPTION_FILTER_DATA_H

#include <obs.h>
#include <util/circlebuf.h>
#include <util/darray.h>
#include <media-io/audio-resampler.h>

#include <whisper.h>

#include <thread>
#include <memory>

#define MAX_PREPROC_CHANNELS 2

#define MT_ obs_module_text

struct transcription_filter_data {
	obs_source_t *context; // obs input source
	size_t channels;       // number of channels
	uint32_t sample_rate;  // input sample rate
	// How many input frames (in input sample rate) are needed for the next whisper frame
	size_t frames;
	// How many ms/frames are needed to overlap with the next whisper frame
	size_t overlap_frames;
	size_t overlap_ms;
	// How many frames were processed in the last whisper frame (this is dynamic)
	size_t last_num_frames;

	/* PCM buffers */
	float *copy_buffers[MAX_PREPROC_CHANNELS];
	DARRAY(float) copy_output_buffers[MAX_PREPROC_CHANNELS];
	struct circlebuf info_buffer;
	struct circlebuf input_buffers[MAX_PREPROC_CHANNELS];

	/* Resampler */
	audio_resampler_t *resampler;

	/* whisper */
	std::string whisper_model_path = "models/ggml-tiny.en.bin";
	struct whisper_context *whisper_context;
	whisper_full_params whisper_params;

	float filler_p_threshold;

	bool do_silence;
	bool vad_enabled;
	int log_level;
	bool log_words;
	bool active;

	// Text source to output the subtitles
	obs_weak_source_t *text_source;
	char *text_source_name;
	std::unique_ptr<std::mutex> text_source_mutex;
	// Callback to set the text in the output text source (subtitles)
	std::function<void(const std::string &str)> setTextCallback;

	// Use std for thread and mutex
	std::thread whisper_thread;

	std::unique_ptr<std::mutex> whisper_buf_mutex;
	std::unique_ptr<std::mutex> whisper_ctx_mutex;
	std::unique_ptr<std::condition_variable> wshiper_thread_cv;
};

// Audio packet info
struct transcription_filter_audio_info {
	uint32_t frames;
	uint64_t timestamp;
};

#endif /* TRANSCRIPTION_FILTER_DATA_H */
