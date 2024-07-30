#include "silero-vad-onnx.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <cstdio>
#include <cstdarg>

#include <obs.h>
#include "plugin-support.h"

// #define __DEBUG_SPEECH_PROB___

// prevent clang-format from reformatting the code
// clang-format off
timestamp_t::timestamp_t(int start_, int end_) : start(start_), end(end_){};

// assignment operator modifies object, therefore non-const
timestamp_t &timestamp_t::operator=(const timestamp_t &a)
{
	start = a.start;
	end = a.end;
	return *this;
};

// equality comparison. doesn't modify object. therefore const.
bool timestamp_t::operator==(const timestamp_t &a) const
{
	return (start == a.start && end == a.end);
};
std::string timestamp_t::string()
{
	std::stringstream ss;
	ss << "timestamp " << start << ", " << end;
	return ss.str();
};

std::string timestamp_t::format(const char *fmt, ...)
{
	char buf[256];

	va_list args;
	va_start(args, fmt);
	const auto r = std::vsnprintf(buf, sizeof buf, fmt, args);
	va_end(args);

	if (r < 0)
		// conversion failed
		return {};

	const size_t len = r;
	if (len < sizeof buf)
		// we fit in the buffer
		return {buf, len};

#if __cplusplus >= 201703L
	// C++17: Create a string and write to its underlying array
	std::string s(len, '\0');
	va_start(args, fmt);
	std::vsnprintf(s.data(), len + 1, fmt, args);
	va_end(args);

	return s;
#else
	// C++11 or C++14: We need to allocate scratch memory
	auto vbuf = std::unique_ptr<char[]>(new char[len + 1]);
	va_start(args, fmt);
	std::vsnprintf(vbuf.get(), len + 1, fmt, args);
	va_end(args);

	return {vbuf.get(), len};
#endif
};

void VadIterator::init_engine_threads(int inter_threads, int intra_threads)
{
	// The method should be called in each thread/proc in multi-thread/proc work
	session_options.SetIntraOpNumThreads(intra_threads);
	session_options.SetInterOpNumThreads(inter_threads);
	session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
};

void VadIterator::init_onnx_model(const SileroString &model_path)
{
	// Init threads = 1 for
	init_engine_threads(1, 1);
	// Load model
	session = std::make_shared<Ort::Session>(env, model_path.c_str(), session_options);
};

void VadIterator::reset_states(bool reset_state)
{
	if (reset_state) {
		// Call reset before each audio start
		std::memset(_state.data(), 0.0f, _state.size() * sizeof(float));
		triggered = false;
	}
	temp_end = 0;
	current_sample = 0;

	prev_end = next_start = 0;

	speeches.clear();
	current_speech = timestamp_t();
};

float VadIterator::predict_one(const std::vector<float> &data)
{
	// Infer
	// Create ort tensors
	input.assign(data.begin(), data.end());
	Ort::Value input_ort = Ort::Value::CreateTensor<float>(memory_info, input.data(),
							       input.size(), input_node_dims, 2);
	Ort::Value state_ort = Ort::Value::CreateTensor<float>(
		memory_info, _state.data(), _state.size(), state_node_dims, 3);
	Ort::Value sr_ort = Ort::Value::CreateTensor<int64_t>(memory_info, sr.data(), sr.size(),
							      sr_node_dims, 1);

	// Clear and add inputs
	ort_inputs.clear();
	ort_inputs.emplace_back(std::move(input_ort));
	ort_inputs.emplace_back(std::move(state_ort));
	ort_inputs.emplace_back(std::move(sr_ort));

	// Infer
	ort_outputs = session->Run(Ort::RunOptions{nullptr}, input_node_names.data(),
				   ort_inputs.data(), ort_inputs.size(), output_node_names.data(),
				   output_node_names.size());

	// Output probability & update h,c recursively
	float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];
	float *stateN = ort_outputs[1].GetTensorMutableData<float>();
	std::memcpy(_state.data(), stateN, size_state * sizeof(float));

	return speech_prob;
}

void VadIterator::predict(const std::vector<float> &data)
{
	const float speech_prob = predict_one(data);

	// Push forward sample index
	current_sample += (unsigned int)window_size_samples;

	// Reset temp_end when > threshold
	if ((speech_prob >= threshold)) {
#ifdef __DEBUG_SPEECH_PROB___
		float speech =
			current_sample -
			window_size_samples; // minus window_size_samples to get precise start time point.
		obs_log(LOG_INFO, "{    start: %.3f s (%.3f) %08d}", 1.0 * speech / sample_rate,
			speech_prob, current_sample - window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
		if (temp_end != 0) {
			temp_end = 0;
			if (next_start < prev_end)
				next_start = current_sample - (unsigned int)window_size_samples;
		}
		if (triggered == false) {
			triggered = true;

			current_speech.start = current_sample - (unsigned int)window_size_samples;
		}
		return;
	}

	if ((triggered == true) &&
	    ((float)(current_sample - current_speech.start) > max_speech_samples)) {
		if (prev_end > 0) {
			current_speech.end = prev_end;
			speeches.push_back(current_speech);
			current_speech = timestamp_t();

			// previously reached silence(< neg_thres) and is still not speech(< thres)
			if (next_start < prev_end)
				triggered = false;
			else {
				current_speech.start = next_start;
			}
			prev_end = 0;
			next_start = 0;
			temp_end = 0;

		} else {
			current_speech.end = current_sample;
			speeches.push_back(current_speech);
			current_speech = timestamp_t();
			prev_end = 0;
			next_start = 0;
			temp_end = 0;
			triggered = false;
		}
		return;
	}
	if ((speech_prob >= (threshold - 0.15)) && (speech_prob < threshold)) {
		if (triggered) {
#ifdef __DEBUG_SPEECH_PROB___
			float speech =
				current_sample -
				window_size_samples; // minus window_size_samples to get precise start time point.
			obs_log(LOG_INFO, "{ speaking: %.3f s (%.3f) %08d}",
				1.0 * speech / sample_rate, speech_prob,
				current_sample - window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
		} else {
#ifdef __DEBUG_SPEECH_PROB___
			float speech =
				current_sample -
				window_size_samples; // minus window_size_samples to get precise start time point.
			obs_log(LOG_INFO, "{  silence: %.3f s (%.3f) %08d}",
				1.0 * speech / sample_rate, speech_prob,
				current_sample - window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
		}
		return;
	}

	// 4) End
	if ((speech_prob < (threshold - 0.15))) {
#ifdef __DEBUG_SPEECH_PROB___
		float speech =
			current_sample - window_size_samples -
			speech_pad_samples; // minus window_size_samples to get precise start time point.
		obs_log(LOG_INFO, "{      end: %.3f s (%.3f) %08d}", 1.0 * speech / sample_rate,
			speech_prob, current_sample - window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
		if (triggered == true) {
			if (temp_end == 0) {
				temp_end = current_sample;
			}
			if (current_sample - temp_end >
			    (unsigned int)min_silence_samples_at_max_speech)
				prev_end = temp_end;
			// a. silence < min_slience_samples, continue speaking
			if ((current_sample - temp_end) < (unsigned int)min_silence_samples) {

			}
			// b. silence >= min_slience_samples, end speaking
			else {
				current_speech.end = temp_end;
				if (current_speech.end - current_speech.start >
				    min_speech_samples) {
					speeches.push_back(current_speech);
					current_speech = timestamp_t();
					prev_end = 0;
					next_start = 0;
					temp_end = 0;
					triggered = false;
				}
			}
		} else {
			// may first windows see end state.
		}
		return;
	}
};

void VadIterator::process(const std::vector<float> &input_wav, bool reset_state)
{
	reset_states(reset_state);

	audio_length_samples = (int)input_wav.size();

	for (int j = 0; j < audio_length_samples; j += (int)window_size_samples) {
		if (j + (int)window_size_samples > audio_length_samples)
			break;
		std::vector<float> r{&input_wav[0] + j, &input_wav[0] + j + window_size_samples};
		predict(r);
	}

	if (current_speech.start >= 0) {
		current_speech.end = audio_length_samples;
		speeches.push_back(current_speech);
		current_speech = timestamp_t();
		prev_end = 0;
		next_start = 0;
		temp_end = 0;
		triggered = false;
	}
};

void VadIterator::process(const std::vector<float> &input_wav, std::vector<float> &output_wav)
{
	process(input_wav);
	collect_chunks(input_wav, output_wav);
}

void VadIterator::collect_chunks(const std::vector<float> &input_wav,
				 std::vector<float> &output_wav)
{
	output_wav.clear();
	for (size_t i = 0; i < speeches.size(); i++) {
#ifdef __DEBUG_SPEECH_PROB___
		obs_log(LOG_INFO, "%s", speeches[i].string().c_str());
#endif //#ifdef __DEBUG_SPEECH_PROB___
		std::vector<float> slice(&input_wav[speeches[i].start],
					 &input_wav[speeches[i].end]);
		output_wav.insert(output_wav.end(), slice.begin(), slice.end());
	}
};

const std::vector<timestamp_t> VadIterator::get_speech_timestamps() const
{
	return speeches;
}

void VadIterator::drop_chunks(const std::vector<float> &input_wav, std::vector<float> &output_wav)
{
	output_wav.clear();
	int current_start = 0;
	for (size_t i = 0; i < speeches.size(); i++) {

		std::vector<float> slice(&input_wav[current_start], &input_wav[speeches[i].start]);
		output_wav.insert(output_wav.end(), slice.begin(), slice.end());
		current_start = speeches[i].end;
	}

	std::vector<float> slice(&input_wav[current_start], &input_wav[input_wav.size()]);
	output_wav.insert(output_wav.end(), slice.begin(), slice.end());
};

VadIterator::VadIterator(const SileroString &ModelPath, int Sample_rate, int windows_frame_size,
			 float Threshold, int min_silence_duration_ms, int speech_pad_ms,
			 int min_speech_duration_ms, float max_speech_duration_s)
{
	init_onnx_model(ModelPath);
	threshold = Threshold;
	sample_rate = Sample_rate;
	sr_per_ms = sample_rate / 1000;

	window_size_samples = windows_frame_size * sr_per_ms;

	min_speech_samples = sr_per_ms * min_speech_duration_ms;
	speech_pad_samples = sr_per_ms * speech_pad_ms;

	max_speech_samples = ((float)sample_rate * max_speech_duration_s -
			      (float)window_size_samples - 2.0f * (float)speech_pad_samples);

	min_silence_samples = sr_per_ms * min_silence_duration_ms;
	min_silence_samples_at_max_speech = sr_per_ms * 98;

	input.resize(window_size_samples);
	input_node_dims[0] = 1;
	input_node_dims[1] = window_size_samples;

	_state.resize(size_state);
	sr.resize(1);
	sr[0] = sample_rate;
};
