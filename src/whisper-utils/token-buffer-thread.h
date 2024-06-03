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

#include "plugin-support.h"

#ifdef _WIN32
typedef std::wstring TokenBufferString;
#else
typedef std::string TokenBufferString;
#endif

struct transcription_filter_data;

enum TokenBufferSegmentation { SEGMENTATION_WORD = 0, SEGMENTATION_TOKEN, SEGMENTATION_SENTENCE };

class TokenBufferThread {
public:
	// default constructor
	TokenBufferThread() = default;

	~TokenBufferThread();
	void initialize(struct transcription_filter_data *gf,
			std::function<void(const std::string &)> callback_, size_t numSentences_,
			size_t numTokensPerSentence_, std::chrono::seconds maxTime_,
			TokenBufferSegmentation segmentation_ = SEGMENTATION_TOKEN);

	void addSentence(const std::string &sentence);
	void clear();
	void stopThread();

	bool isEnabled() const { return !stop; }

	void setNumSentences(size_t numSentences_) { numSentences = numSentences_; }
	void setNumPerSentence(size_t numPerSentence_) { numPerSentence = numPerSentence_; }

private:
	void monitor();
	void log_token_vector(const std::vector<std::string> &tokens);
	struct transcription_filter_data *gf;
	std::deque<TokenBufferString> inputQueue;
	std::deque<TokenBufferString> presentationQueue;
	std::thread workerThread;
	std::mutex inputQueueMutex;
	std::mutex presentationQueueMutex;
	std::condition_variable condVar;
	std::function<void(std::string)> callback;
	std::chrono::seconds maxTime;
	bool stop = true;
	bool newDataAvailable = false;
	size_t numSentences;
	size_t numPerSentence;
	TokenBufferSegmentation segmentation;
};

#endif
