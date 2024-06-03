#include <ctranslate2/translation.h>
#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include "token-buffer-thread.h"
#include "whisper-utils.h"

#include <obs-module.h>

#ifdef _WIN32
#include <Windows.h>
#define SPACE L" "
#define NEWLINE L"\n"
#else
#define SPACE " "
#define NEWLINE "\n"
#endif

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
				   size_t numSentences_, size_t numPerSentence_,
				   std::chrono::seconds maxTime_,
				   TokenBufferSegmentation segmentation_)
{
	this->gf = gf_;
	this->callback = callback_;
	this->numSentences = numSentences_;
	this->numPerSentence = numPerSentence_;
	this->segmentation = segmentation_;
	this->maxTime = maxTime_;
	this->initialized = true;
	this->workerThread = std::thread(&TokenBufferThread::monitor, this);
}

void TokenBufferThread::log_token_vector(const std::vector<std::string> &tokens)
{
	std::string output;
	for (const auto &token : tokens) {
		output += token;
	}
	obs_log(LOG_INFO, "TokenBufferThread::log_token_vector: '%s'", output.c_str());
}

void TokenBufferThread::addSentence(const std::string &sentence)
{
	{
		std::lock_guard<std::mutex> lock(queueMutex);

#ifdef _WIN32
		// on windows convert from multibyte to wide char
		int count = MultiByteToWideChar(CP_UTF8, 0, sentence.c_str(),
						(int)sentence.length(), NULL, 0);
		std::wstring sentence_ws(count, 0);
		MultiByteToWideChar(CP_UTF8, 0, sentence.c_str(), (int)sentence.length(),
				    &sentence_ws[0], count);
		// split to characters
		std::vector<std::wstring> characters;
		for (const auto &c : sentence_ws) {
			characters.push_back(std::wstring(1, c));
		}
#else
		// split to characters
		std::vector<std::string> characters;
		for (const auto &c : sentence_ws) {
			characters.push_back(std::string(1, c));
		}
#endif

		// add the reconstructed sentence to the wordQueue
		for (const auto &character : characters) {
			inputQueue.push_back(character);
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
		this->condVar.wait_for(lock, std::chrono::milliseconds(100),
				       [this] { return this->stop; });

		if (this->stop) {
			break;
		}

		if (this->gf->whisper_context == nullptr) {
			continue;
		}

		// condition presentation queue
		if (presentationQueue.size() == this->numSentences * this->numPerSentence) {
			// pop a whole sentence from the presentation queue front
			for (size_t i = 0; i < this->numPerSentence; i++) {
				presentationQueue.pop_front();
			}
		}

		if (!inputQueue.empty()) {
			// if there are token on the input queue
			if (this->segmentation == SEGMENTATION_SENTENCE) {
				// add all the tokens from the input queue to the presentation queue
				for (const auto &token : inputQueue) {
					presentationQueue.push_back(token);
				}
			} else {
				// add one token to the presentation queue
				presentationQueue.push_back(inputQueue.front());
				inputQueue.pop_front();
			}
		} else {
			// if there are no tokens on the input queue
			continue;
		}

		if (presentationQueue.size() > 0) {
			// build a caption from the presentation queue in sentences
			// with a maximum of numPerSentence tokens/words per sentence
			// and a newline between sentences
			TokenBufferString caption;
			if (this->segmentation == SEGMENTATION_WORD) {
				// iterate through the presentation queue tokens and make words (based on spaces)
				// then build a caption with a maximum of numPerSentence words per sentence
				size_t wordsInSentence = 0;
				TokenBufferString word;
				for (const auto &token : presentationQueue) {
					// keep adding tokens to the word until a space is found
					word += token;
					if (word.find(SPACE) != TokenBufferString::npos) {
						// cut the word at the space and add it to the caption
						caption += word.substr(0, word.find(SPACE));
						wordsInSentence++;
						// keep the rest of the word for the next iteration
						word = word.substr(word.find(SPACE) + 1);

						if (wordsInSentence == this->numPerSentence) {
							caption += word;
							caption += SPACE;
							wordsInSentence = 0;
							word.clear();
						}
					}
				}
			} else {
				// iterate through the presentation queue tokens and build a caption
				size_t tokensInSentence = 0;
				for (const auto &token : presentationQueue) {
					caption += token;
					tokensInSentence++;
					if (tokensInSentence == this->numPerSentence) {
						caption += NEWLINE;
						tokensInSentence = 0;
					}
				}
			}

#ifdef _WIN32
			// convert caption to multibyte for obs
			int count = WideCharToMultiByte(CP_UTF8, 0, caption.c_str(),
							(int)caption.length(), NULL, 0, NULL, NULL);
			std::string caption_out(count, 0);
			WideCharToMultiByte(CP_UTF8, 0, caption.c_str(), (int)caption.length(),
					    &caption_out[0], count, NULL, NULL);
#else
			std::string caption_out(caption.begin(), caption.end());
#endif

			// emit the caption
			this->callback(caption_out);
		}
	}
	obs_log(LOG_INFO, "TokenBufferThread::monitor: done");
}
