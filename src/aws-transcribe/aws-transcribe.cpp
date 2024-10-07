#include <util/base.h>

#include "aws-transcribe.h"
#include "plugin-support.h"
#include "whisper-utils/resample-utils.h"
#include "transcription-utils.h"

#include <nlohmann/json.hpp>
#include <queue>
#include <mutex>
#include <future>
#include <thread>

using json = nlohmann::json;

class TranscriptionHandler::Impl {
public:
	using MessageCallback = TranscriptionHandler::MessageCallback;

	explicit Impl(transcription_filter_data *gf_, MessageCallback callback)
		: gf(gf_),
		  messageCallback(callback),
		  running(false)
	{
	}

	void start()
	{
		if (!running) {
			running = true;
			processingThread =
				std::async(std::launch::async, [this]() { processAudioQueue(); });
		}
	}

	void stop()
	{
		if (running) {
			running = false;
			if (processingThread.valid())
				processingThread.wait();
		}
	}

private:
	transcription_filter_data *gf;
	MessageCallback messageCallback;
	std::queue<std::vector<int16_t>> audioQueue;
	std::mutex mutex;
	std::atomic<bool> running;
	std::future<void> processingThread;

	void handleIncomingMessage(const std::string &message)
	{
		try {
			json j = json::parse(message);

			// TODO: handle incoming message from AWS Transcribe

			// messageCallback(type, text, start_timestamp, end_timestamp);
		} catch (json::parse_error &e) {
			obs_log(LOG_ERROR, "Failed to parse JSON message: %s", e.what());
		} catch (json::type_error &e) {
			obs_log(LOG_ERROR, "Failed to parse JSON message: %s", e.what());
		}
	}

	void processAudioQueue()
	{
		while (running) {
			// get data from buffer and resample to 16kHz
			uint64_t start_timestamp_offset_ns = 0;
			uint64_t end_timestamp_offset_ns = 0;

			const int ret = get_data_from_buf_and_resample(
				gf, start_timestamp_offset_ns, end_timestamp_offset_ns);
			if (ret != 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			std::vector<float> audio_input;
			audio_input.resize(gf->resampled_buffer.size / sizeof(float));
			circlebuf_pop_front(&gf->resampled_buffer, audio_input.data(),
					    audio_input.size() * sizeof(float));

			std::vector<int16_t> pcmData(audio_input.size());
			for (size_t i = 0; i < audio_input.size(); ++i) {
				pcmData[i] = static_cast<int16_t>(audio_input[i] * 32767.0f);
			}

			if (!pcmData.empty()) {
				// TODO: prepare data to send to AWS Transcribe
				sendAudioData(pcmData);
			} else {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			if (!gf->cleared_last_sub) {
				// check if we should clear the current sub depending on the minimum subtitle duration
				uint64_t now = now_ms();
				if ((now - gf->last_sub_render_time) > gf->max_sub_duration) {
					// clear the current sub, call the callback with an empty string
					obs_log(gf->log_level,
						"Clearing current subtitle. now: %lu ms, last: %lu ms",
						now, gf->last_sub_render_time);
					clear_current_caption(gf);
				}
			}
		}
	}

	std::vector<int16_t> audioBuffer;

	void sendAudioData(const std::vector<int16_t> &audioData)
	{
		std::lock_guard<std::mutex> lock(mutex);
		audioBuffer.insert(audioBuffer.end(), audioData.begin(), audioData.end());

		// TODO send audio data to AWS Transcribe

		audioBuffer.clear();
	}
};

TranscriptionHandler::TranscriptionHandler(transcription_filter_data *gf_, MessageCallback callback)
	: pimpl(std::make_unique<Impl>(std::move(gf_), std::move(callback)))
{
}

TranscriptionHandler::~TranscriptionHandler() = default;

TranscriptionHandler::TranscriptionHandler(TranscriptionHandler &&) noexcept = default;
TranscriptionHandler &TranscriptionHandler::operator=(TranscriptionHandler &&) noexcept = default;

void TranscriptionHandler::start()
{
	pimpl->start();
}
void TranscriptionHandler::stop()
{
	pimpl->stop();
}
