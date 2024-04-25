#ifndef TOKEN_BUFFER_THREAD_H
#define TOKEN_BUFFER_THREAD_H

#include <queue>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>

#include <obs.h>

#include <whisper.h>

#include "plugin-support.h"

struct transcription_filter_data;

class TokenBufferThread {
public:
	// default constructor
	TokenBufferThread() = default;

	~TokenBufferThread();
	void initialize(struct transcription_filter_data *gf,
			std::function<void(const std::string &)> callback_, size_t maxSize_,
			std::chrono::seconds maxTime_);

	void addWords(const std::vector<whisper_token_data> &words);

private:
	void monitor();
	void log_token_vector(const std::vector<whisper_token_data> &tokens);
	struct transcription_filter_data *gf;
	std::deque<whisper_token_data> wordQueue;
	std::thread workerThread;
	std::mutex queueMutex;
	std::condition_variable condVar;
	std::function<void(std::string)> callback;
	size_t maxSize;
	std::chrono::seconds maxTime;
	bool stop;
	bool initialized = false;
	bool newDataAvailable = false;
};

#endif
