#ifndef CAPTIONS_THREAD_H
#define CAPTIONS_THREAD_H

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

class CaptionMonitor {
public:
	// default constructor
	CaptionMonitor() = default;

	~CaptionMonitor()
	{
		{
			std::lock_guard<std::mutex> lock(queueMutex);
			stop = true;
		}
		condVar.notify_all();
		workerThread.join();
	}

	void initialize(std::function<void(const std::string &)> callback_, size_t maxSize_,
			std::chrono::seconds maxTime_)
	{
		obs_log(LOG_INFO, "CaptionMonitor::initialize");
		this->callback = callback_;
		this->maxSize = maxSize_;
		this->maxTime = maxTime_;
		this->initialized = true;
		this->workerThread = std::thread(&CaptionMonitor::monitor, this);
	}

	void addWords(const std::vector<std::string> &words)
	{
		{
			std::lock_guard<std::mutex> lock(queueMutex);
			for (const auto &word : words) {
				wordQueue.push_back(word);
			}
			this->newDataAvailable = true;
		}
		obs_log(LOG_INFO, "CaptionMonitor::addWords: number of words in queue: %d",
			wordQueue.size());
		condVar.notify_all();
	}

private:
	void monitor()
	{
		obs_log(LOG_INFO, "CaptionMonitor::monitor");
		auto startTime = std::chrono::steady_clock::now();
		while (true) {
			std::unique_lock<std::mutex> lock(this->queueMutex);
			// wait for new data or stop signal
			this->condVar.wait(lock,
					   [this] { return this->newDataAvailable || this->stop; });

			if (this->stop) {
				obs_log(LOG_INFO, "CaptionMonitor::monitor: stopping");
				break;
			}

			if (this->wordQueue.empty()) {
				continue;
			}

			obs_log(LOG_INFO, "CaptionMonitor::monitor: wordQueue size: %d",
				this->wordQueue.size());

			// emit up to maxSize words from the wordQueue
			std::vector<std::string> emitted;
			while (!this->wordQueue.empty() && emitted.size() <= this->maxSize) {
				emitted.push_back(this->wordQueue.front());
				this->wordQueue.pop_front();
			}
			// emit the caption, joining the words with a space
			std::string output;
			for (const auto &word : emitted) {
				output += word + " ";
			}
			this->callback(output);
			// push back the words that were emitted, in reverse order
			for (auto it = emitted.rbegin(); it != emitted.rend(); ++it) {
				this->wordQueue.push_front(*it);
			}

			if (this->wordQueue.size() >= this->maxSize ||
			    std::chrono::steady_clock::now() - startTime >= this->maxTime) {
				// flush the queue if it's full or we've reached the max time
				size_t words_to_flush =
					std::min(this->wordQueue.size(), this->maxSize);
				obs_log(LOG_INFO, "CaptionMonitor::monitor: flushing %d words",
					words_to_flush);
				for (size_t i = 0; i < words_to_flush; ++i) {
					wordQueue.pop_front();
				}
				startTime = std::chrono::steady_clock::now();
			}

			newDataAvailable = false;
		}
		obs_log(LOG_INFO, "CaptionMonitor::monitor: done");
	}

	std::deque<std::string> wordQueue;
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

#endif // CAPTIONS_THREAD_H
