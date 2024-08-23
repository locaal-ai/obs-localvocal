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
typedef wchar_t TokenBufferChar;
#else
typedef std::string TokenBufferString;
typedef char TokenBufferChar;
#endif

struct transcription_filter_data;

enum TokenBufferSegmentation { SEGMENTATION_WORD = 0, SEGMENTATION_TOKEN, SEGMENTATION_SENTENCE };
enum TokenBufferSpeed { SPEED_SLOW = 0, SPEED_NORMAL, SPEED_FAST };

typedef std::chrono::time_point<std::chrono::steady_clock> TokenBufferTimePoint;

inline std::chrono::time_point<std::chrono::steady_clock> get_time_point_from_ms(uint64_t ms)
{
	return std::chrono::time_point<std::chrono::steady_clock>(std::chrono::milliseconds(ms));
}

struct TokenBufferToken {
	TokenBufferString token;
	bool is_partial;
};

struct TokenBufferSentence {
	std::vector<TokenBufferToken> tokens;
	TokenBufferTimePoint start_time;
	TokenBufferTimePoint end_time;
};

class TokenBufferThread {
public:
	// default constructor
	TokenBufferThread() noexcept;

	~TokenBufferThread();
	void initialize(struct transcription_filter_data *gf,
			std::function<void(const std::string &)> captionPresentationCallback_,
			std::function<void(const std::string &)> sentenceOutputCallback_,
			size_t numSentences_, size_t numTokensPerSentence_,
			std::chrono::seconds maxTime_,
			TokenBufferSegmentation segmentation_ = SEGMENTATION_TOKEN);

	void addSentenceFromStdString(const std::string &sentence, TokenBufferTimePoint start_time,
				      TokenBufferTimePoint end_time, bool is_partial = false);
	void addSentence(const TokenBufferSentence &sentence);
	void clear();
	void stopThread();

	bool isEnabled() const { return !stop; }

	void setNumSentences(size_t numSentences_) { numSentences = numSentences_; }
	void setNumPerSentence(size_t numPerSentence_) { numPerSentence = numPerSentence_; }
	void setMaxTime(std::chrono::seconds maxTime_) { maxTime = maxTime_; }
	void setSegmentation(TokenBufferSegmentation segmentation_)
	{
		segmentation = segmentation_;
	}

private:
	void monitor();
	void log_token_vector(const std::vector<std::string> &tokens);
	int getWaitTime(TokenBufferSpeed speed) const;
	struct transcription_filter_data *gf;
	std::deque<TokenBufferToken> inputQueue;
	std::deque<TokenBufferToken> presentationQueue;
	std::deque<TokenBufferToken> contributionQueue;
	std::thread workerThread;
	std::mutex inputQueueMutex;
	std::mutex presentationQueueMutex;
	std::function<void(std::string)> captionPresentationCallback;
	std::function<void(std::string)> sentenceOutputCallback;
	std::condition_variable cv;
	std::chrono::seconds maxTime;
	std::atomic<bool> stop;
	bool newDataAvailable = false;
	size_t numSentences;
	size_t numPerSentence;
	TokenBufferSegmentation segmentation;
	// timestamp of the last caption
	TokenBufferTimePoint lastCaptionTime;
	// timestamp of the last contribution
	TokenBufferTimePoint lastContributionTime;
	bool lastContributionIsSent = false;
	std::string lastCaption;
};

#endif
