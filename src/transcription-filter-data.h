#ifndef TRANSCRIPTION_FILTER_DATA_H
#define TRANSCRIPTION_FILTER_DATA_H

#include <obs.h>
#include <util/circlebuf.h>
#include <util/darray.h>
#include <media-io/audio-resampler.h>

#include <whisper.h>

#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>

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
	// Milliseconds per processing step (e.g. rest of the whisper buffer may be filled with silence)
	size_t step_size_msec;

	/* PCM buffers */
	float *copy_buffers[MAX_PREPROC_CHANNELS];
	struct circlebuf info_buffer;
	struct circlebuf input_buffers[MAX_PREPROC_CHANNELS];

	/* Resampler */
	audio_resampler_t *resampler = nullptr;

	/* whisper */
	std::string whisper_model_path = "models/ggml-tiny.en.bin";
	struct whisper_context *whisper_context = nullptr;
	whisper_full_params whisper_params;

	float filler_p_threshold;

	bool do_silence;
	bool vad_enabled;
	int log_level;
	bool log_words;
	bool caption_to_stream;
	bool active = false;

	// Text source to output the subtitles
	obs_weak_source_t *text_source = nullptr;
	char *text_source_name = nullptr;
	std::mutex *text_source_mutex = nullptr;
	// Callback to set the text in the output text source (subtitles)
	std::function<void(const std::string &str)> setTextCallback;
	// Output file path to write the subtitles
	std::string output_file_path;

	// Use std for thread and mutex
	std::thread whisper_thread;

	std::mutex *whisper_buf_mutex = nullptr;
	std::mutex *whisper_ctx_mutex = nullptr;
	std::condition_variable *wshiper_thread_cv = nullptr;
};

// Audio packet info
struct transcription_filter_audio_info {
	uint32_t frames;
	uint64_t timestamp;
};

void set_text_callback(struct transcription_filter_data *gf, const std::string &str);

#endif /* TRANSCRIPTION_FILTER_DATA_H */
