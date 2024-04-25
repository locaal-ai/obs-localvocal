#include "token-buffer-thread.h"
#include "./whisper-utils.h"

TokenBufferThread::~TokenBufferThread()
{
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		stop = true;
	}
	condVar.notify_all();
	workerThread.join();
}

void TokenBufferThread::initialize(struct transcription_filter_data *gf_,
				   std::function<void(const std::string &)> callback_,
				   size_t maxSize_, std::chrono::seconds maxTime_)
{
	this->gf = gf_;
	this->callback = callback_;
	this->maxSize = maxSize_;
	this->maxTime = maxTime_;
	this->initialized = true;
	this->workerThread = std::thread(&TokenBufferThread::monitor, this);
}

void TokenBufferThread::log_token_vector(const std::vector<whisper_token_data> &tokens)
{
	std::string output;
	for (const auto &token : tokens) {
		const char *token_str = whisper_token_to_str(gf->whisper_context, token.id);
		output += token_str;
	}
	obs_log(LOG_INFO, "TokenBufferThread::log_token_vector: '%s'", output.c_str());
}

void TokenBufferThread::addWords(const std::vector<whisper_token_data> &words)
{
	obs_log(LOG_INFO, "TokenBufferThread::addWords");
	{
		std::lock_guard<std::mutex> lock(queueMutex);

		// convert current wordQueue to vector
		std::vector<whisper_token_data> currentWords(wordQueue.begin(), wordQueue.end());

		log_token_vector(currentWords);
		log_token_vector(words);

		// run reconstructSentence
		std::vector<whisper_token_data> reconstructed =
			reconstructSentence(currentWords, words);

		log_token_vector(reconstructed);

		// clear the wordQueue
		wordQueue.clear();

		// add the reconstructed sentence to the wordQueue
		for (const auto &word : reconstructed) {
			wordQueue.push_back(word);
		}

		newDataAvailable = true;
	}
	condVar.notify_all();
}

void TokenBufferThread::monitor()
{
	obs_log(LOG_INFO, "TokenBufferThread::monitor");
	auto startTime = std::chrono::steady_clock::now();
	while (this->initialized && !this->stop) {
		std::unique_lock<std::mutex> lock(this->queueMutex);
		// wait for new data or stop signal
		this->condVar.wait(lock, [this] { return this->newDataAvailable || this->stop; });

		if (this->stop) {
			break;
		}

		if (this->wordQueue.empty()) {
			continue;
		}

		if (this->gf->whisper_context == nullptr) {
			continue;
		}

		// emit up to maxSize words from the wordQueue
		std::vector<whisper_token_data> emitted;
		while (!this->wordQueue.empty() && emitted.size() <= this->maxSize) {
			emitted.push_back(this->wordQueue.front());
			this->wordQueue.pop_front();
		}
		obs_log(LOG_INFO, "TokenBufferThread::monitor: emitting %d words", emitted.size());
		log_token_vector(emitted);
		// emit the caption from the tokens
		std::string output;
		for (const auto &token : emitted) {
			const char *token_str =
				whisper_token_to_str(this->gf->whisper_context, token.id);
			output += token_str;
		}
		this->callback(output);
		// push back the words that were emitted, in reverse order
		for (auto it = emitted.rbegin(); it != emitted.rend(); ++it) {
			this->wordQueue.push_front(*it);
		}

		// check if we need to flush the queue
		auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now() - startTime);
		if (this->wordQueue.size() >= this->maxSize || elapsedTime >= this->maxTime) {
			// flush the queue if it's full or we've reached the max time
			size_t words_to_flush = std::min(this->wordQueue.size(), this->maxSize);
			// make sure we leave at least 3 words in the queue
			size_t words_remaining = this->wordQueue.size() - words_to_flush;
			if (words_remaining < 3) {
				words_to_flush -= 3 - words_remaining;
			}
			obs_log(LOG_INFO, "TokenBufferThread::monitor: flushing %d words",
				words_to_flush);
			for (size_t i = 0; i < words_to_flush; ++i) {
				wordQueue.pop_front();
			}
			startTime = std::chrono::steady_clock::now();
		}

		newDataAvailable = false;
	}
	obs_log(LOG_INFO, "TokenBufferThread::monitor: done");
}
