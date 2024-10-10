#include <util/base.h>

#include "stenographer.h"
#include "plugin-support.h"
#include "whisper-utils/resample-utils.h"
#include "transcription-utils.h"

#define ASIO_STANDALONE
#define _WEBSOCKETPP_CPP11_TYPE_TRAITS_

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <queue>
#include <mutex>
#include <future>
#include <thread>

using json = nlohmann::json;
typedef websocketpp::server<websocketpp::config::asio> server;

// WAV header structure
struct WAVHeader {
	char riff[4] = {'R', 'I', 'F', 'F'};
	uint32_t overall_size;
	char wave[4] = {'W', 'A', 'V', 'E'};
	char fmt_chunk_marker[4] = {'f', 'm', 't', ' '};
	uint32_t length_of_fmt = 16;
	uint16_t format_type = 1;
	uint16_t channels = 1;
	uint32_t sample_rate = 16000;
	uint32_t byterate;
	uint16_t block_align;
	uint16_t bits_per_sample = 16;
	char data_chunk_header[4] = {'d', 'a', 't', 'a'};
	uint32_t data_size;
};

class TranscriptionHandler::Impl {
public:
	using MessageCallback = TranscriptionHandler::MessageCallback;

	explicit Impl(transcription_filter_data *gf_, MessageCallback callback)
		: gf(gf_),
		  messageCallback(callback),
		  running(false)
	{
		server.init_asio();

		server.set_open_handler([this](websocketpp::connection_hdl hdl) {
			std::lock_guard<std::mutex> lock(mutex);
			connection = hdl;
		});

		server.set_message_handler(
			[this](websocketpp::connection_hdl hdl, server::message_ptr msg) {
				UNUSED_PARAMETER(hdl);
				handleIncomingMessage(msg->get_payload());
			});

		// Initialize WAV header
		wavHeader.byterate =
			wavHeader.sample_rate * wavHeader.channels * wavHeader.bits_per_sample / 8;
		wavHeader.block_align = wavHeader.channels * wavHeader.bits_per_sample / 8;
	}

	void start()
	{
		if (!running) {
			running = true;
			serverThread = std::async(std::launch::async, [this]() {
				server.listen(9002);
				server.start_accept();
				server.run();
			});

			processingThread =
				std::async(std::launch::async, [this]() { processAudioQueue(); });
		}
	}

	void stop()
	{
		if (running) {
			running = false;
			server.stop();
			if (serverThread.valid())
				serverThread.wait();
			if (processingThread.valid())
				processingThread.wait();
		}
	}

private:
	transcription_filter_data *gf;
	server server;
	websocketpp::connection_hdl connection;
	MessageCallback messageCallback;
	std::queue<std::vector<int16_t>> audioQueue;
	std::mutex mutex;
	std::atomic<bool> running;
	std::future<void> serverThread;
	std::future<void> processingThread;

	void handleIncomingMessage(const std::string &message)
	{
		try {
			json j = json::parse(message);
			std::string type = j["type"].get<std::string>();
			std::string text = j["text"].get<std::string>();

			uint64_t start_timestamp = j["start_timestamp"].get<uint64_t>();
			uint64_t end_timestamp = j["end_timestamp"].get<uint64_t>();

			messageCallback(type, text, start_timestamp, end_timestamp);
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
				json timestampInfo = {{"start_timestamp",
						       start_timestamp_offset_ns},
						      {"end_timestamp", end_timestamp_offset_ns}};
				if (connection.lock()) {
					server.send(connection, timestampInfo.dump(),
						    websocketpp::frame::opcode::text);
				}
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

	WAVHeader wavHeader;
	std::vector<int16_t> audioBuffer;

	void sendAudioData(const std::vector<int16_t> &audioData)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (connection.lock()) {
			audioBuffer.insert(audioBuffer.end(), audioData.begin(), audioData.end());

			// If we have accumulated enough data, send it as a WAV file
			if (audioBuffer.size() >= 8000) { // 0.5 seconds of audio at 16kHz
				wavHeader.data_size = audioBuffer.size() * sizeof(int16_t);
				wavHeader.overall_size =
					wavHeader.data_size + sizeof(WAVHeader) - 8;

				std::vector<uint8_t> wavData(sizeof(WAVHeader) +
							     wavHeader.data_size);
				std::memcpy(wavData.data(), &wavHeader, sizeof(WAVHeader));
				std::memcpy(wavData.data() + sizeof(WAVHeader), audioBuffer.data(),
					    wavHeader.data_size);

				server.send(connection, wavData.data(), wavData.size(),
					    websocketpp::frame::opcode::binary);

				audioBuffer.clear();
			}
		}
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
