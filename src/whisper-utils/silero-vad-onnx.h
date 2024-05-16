#ifndef SILERO_VAD_ONNX_H
#define SILERO_VAD_ONNX_H

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <limits>

#ifdef _WIN32
typedef std::wstring SileroString;
#else
typedef std::string SileroString;
#endif

class timestamp_t {
public:
	int start;
	int end;

	// default + parameterized constructor
	timestamp_t(int start = -1, int end = -1);

	// assignment operator modifies object, therefore non-const
	timestamp_t &operator=(const timestamp_t &a);

	// equality comparison. doesn't modify object. therefore const.
	bool operator==(const timestamp_t &a) const;
	std::string string();

private:
	std::string format(const char *fmt, ...);
};

class VadIterator {
private:
	// OnnxRuntime resources
	Ort::Env env;
	Ort::SessionOptions session_options;
	std::shared_ptr<Ort::Session> session = nullptr;
	Ort::AllocatorWithDefaultOptions allocator;
	Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);

private:
	void init_engine_threads(int inter_threads, int intra_threads);
	void init_onnx_model(const SileroString &model_path);
	void reset_states(bool reset_hc);
	float predict_one(const std::vector<float> &data);
	void predict(const std::vector<float> &data);

public:
	void process(const std::vector<float> &input_wav, bool reset_hc = true);
	void process(const std::vector<float> &input_wav, std::vector<float> &output_wav);
	void collect_chunks(const std::vector<float> &input_wav, std::vector<float> &output_wav);
	const std::vector<timestamp_t> get_speech_timestamps() const;
	void drop_chunks(const std::vector<float> &input_wav, std::vector<float> &output_wav);

private:
	// model config
	int64_t window_size_samples; // Assign when init, support 256 512 768 for 8k; 512 1024 1536 for 16k.
	int sample_rate;             // Assign when init support 16000 or 8000
	int sr_per_ms;               // Assign when init, support 8 or 16
	float threshold;
	int min_silence_samples;               // sr_per_ms * #ms
	int min_silence_samples_at_max_speech; // sr_per_ms * #98
	int min_speech_samples;                // sr_per_ms * #ms
	float max_speech_samples;
	int speech_pad_samples; // usually a
	int audio_length_samples;

	// model states
	bool triggered = false;
	unsigned int temp_end = 0;
	unsigned int current_sample = 0;
	// MAX 4294967295 samples / 8sample per ms / 1000 / 60 = 8947 minutes
	int prev_end;
	int next_start = 0;

	//Output timestamp
	std::vector<timestamp_t> speeches;
	timestamp_t current_speech;

	// Onnx model
	// Inputs
	std::vector<Ort::Value> ort_inputs;

	std::vector<const char *> input_node_names = {"input", "sr", "h", "c"};
	std::vector<float> input;
	std::vector<int64_t> sr;
	unsigned int size_hc = 2 * 1 * 64; // It's FIXED.
	std::vector<float> _h;
	std::vector<float> _c;

	int64_t input_node_dims[2] = {};
	const int64_t sr_node_dims[1] = {1};
	const int64_t hc_node_dims[3] = {2, 1, 64};

	// Outputs
	std::vector<Ort::Value> ort_outputs;
	std::vector<const char *> output_node_names = {"output", "hn", "cn"};

public:
	// Construction
	VadIterator(const SileroString &ModelPath, int Sample_rate = 16000,
		    int windows_frame_size = 64, float Threshold = 0.5,
		    int min_silence_duration_ms = 0, int speech_pad_ms = 64,
		    int min_speech_duration_ms = 64,
		    float max_speech_duration_s = std::numeric_limits<float>::infinity());

	// Default constructor
	VadIterator() = default;
};

#endif // SILERO_VAD_ONNX_H
