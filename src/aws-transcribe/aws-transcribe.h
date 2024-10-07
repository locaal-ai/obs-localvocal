#pragma once

// Forward declaration
struct transcription_filter_data;

#include <functional>
#include <memory>
#include <string>

class TranscriptionHandler {
public:
	using MessageCallback =
		std::function<void(const std::string &type, const std::string &text,
				   uint64_t start_timestamp, uint64_t end_timestamp)>;

	explicit TranscriptionHandler(transcription_filter_data *gf_, MessageCallback callback);
	~TranscriptionHandler();

	// Disable copy
	TranscriptionHandler(const TranscriptionHandler &) = delete;
	TranscriptionHandler &operator=(const TranscriptionHandler &) = delete;

	// Enable move
	TranscriptionHandler(TranscriptionHandler &&) noexcept;
	TranscriptionHandler &operator=(TranscriptionHandler &&) noexcept;

	void start();
	void stop();

private:
	class Impl;
	std::unique_ptr<Impl> pimpl;
};