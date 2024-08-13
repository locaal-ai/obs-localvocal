#ifndef CLOUD_TRANSCRIPTION_H
#define CLOUD_TRANSCRIPTION_H

#include <string>
#include <thread>

#include "plugin-support.h"

#include <obs.h>

class CloudTranscription {
public:
	CloudTranscription();
	~CloudTranscription();

	void startTranscription(const std::string &audioFile);
	void stopTranscription();

	void setLanguage(const std::string &language)
	{
		obs_log(LOG_INFO, "Setting language to %s", language.c_str());
	}

private:
	std::thread transcriptionThread;
	bool isTranscribing;
	std::string language;

	void transcriptionWorker(const std::string &audioFile);
};

#endif // CLOUD_TRANSCRIPTION_H
