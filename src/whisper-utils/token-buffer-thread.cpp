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

TokenBufferThread::TokenBufferThread() noexcept
	: gf(nullptr),
	  numSentences(1),
	  numPerSentence(1),
	  maxTime(0),
	  stop(true),
	  presentationQueueMutex(),
	  inputQueueMutex()
{
}

TokenBufferThread::~TokenBufferThread()
{
	stopThread();
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
	this->stop = false;
	this->workerThread = std::thread(&TokenBufferThread::monitor, this);
}

void TokenBufferThread::stopThread()
{
	{
		std::lock_guard<std::mutex> lock(presentationQueueMutex);
		stop = true;
	}
	cv.notify_all();
	if (workerThread.joinable()) {
		workerThread.join();
	}
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
#ifdef _WIN32
	// on windows convert from multibyte to wide char
	int count =
		MultiByteToWideChar(CP_UTF8, 0, sentence.c_str(), (int)sentence.length(), NULL, 0);
	std::wstring sentence_ws(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, sentence.c_str(), (int)sentence.length(), &sentence_ws[0],
			    count);
	// split to characters
	std::vector<std::wstring> characters;
	for (const auto &c : sentence_ws) {
		characters.push_back(std::wstring(1, c));
	}
#else
	// split to characters
	std::vector<std::string> characters;
	for (const auto &c : sentence) {
		characters.push_back(std::string(1, c));
	}
#endif

	std::lock_guard<std::mutex> lock(inputQueueMutex);

	// add the reconstructed sentence to the wordQueue
	for (const auto &character : characters) {
		inputQueue.push_back(character);
	}
	inputQueue.push_back(SPACE);
}

void TokenBufferThread::clear()
{
	{
		std::lock_guard<std::mutex> lock(inputQueueMutex);
		inputQueue.clear();
	}
	{
		std::lock_guard<std::mutex> lock(presentationQueueMutex);
		presentationQueue.clear();
	}
	this->callback("");
}

void TokenBufferThread::monitor()
{
	obs_log(LOG_INFO, "TokenBufferThread::monitor");

	this->callback("");

	while (true) {
		std::string caption_out;

		{
			std::lock_guard<std::mutex> lockPresentation(presentationQueueMutex);
			if (stop) {
				break;
			}

			// condition presentation queue
			if (presentationQueue.size() == this->numSentences * this->numPerSentence) {
				// pop a whole sentence from the presentation queue front
				for (size_t i = 0; i < this->numPerSentence; i++) {
					presentationQueue.pop_front();
				}
			}

			{
				std::lock_guard<std::mutex> lock(inputQueueMutex);

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
				}
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

							if (wordsInSentence ==
							    this->numPerSentence) {
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
						// skip spaces in the beginning of a sentence (tokensInSentence == 0)
						if (token == SPACE && tokensInSentence == 0) {
							continue;
						}

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
								(int)caption.length(), NULL, 0,
								NULL, NULL);
				caption_out = std::string(count, 0);
				WideCharToMultiByte(CP_UTF8, 0, caption.c_str(),
						    (int)caption.length(), &caption_out[0], count,
						    NULL, NULL);
#else
				caption_out = std::string(caption.begin(), caption.end());
#endif
			}
		}

		if (this->stop) {
			break;
		}

		if (caption_out.empty()) {
			// if no caption was built, sleep for a while
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		// emit the caption
		this->callback(caption_out);

		// check the input queue size (iqs), if it's big - sleep less
		std::this_thread::sleep_for(std::chrono::milliseconds(inputQueue.size() > 30 ? 33
								      : inputQueue.size() > 15
									      ? 66
									      : 100));
	}

	obs_log(LOG_INFO, "TokenBufferThread::monitor: done");
}
