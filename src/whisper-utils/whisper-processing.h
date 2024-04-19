#ifndef WHISPER_PROCESSING_H
#define WHISPER_PROCESSING_H

#include <whisper.h>

// buffer size in msec
#define DEFAULT_BUFFER_SIZE_MSEC 3000
// overlap in msec
#define DEFAULT_OVERLAP_SIZE_MSEC 1000

enum DetectionResult {
	DETECTION_RESULT_UNKNOWN = 0,
	DETECTION_RESULT_SILENCE = 1,
	DETECTION_RESULT_SPEECH = 2,
};

struct DetectionResultWithText {
	DetectionResult result;
	std::string text;
	uint64_t start_timestamp_ms;
	uint64_t end_timestamp_ms;
    std::vector<whisper_token_data> tokens;
};

void whisper_loop(void *data);
struct whisper_context *init_whisper_context(const std::string &model_path,
					     struct transcription_filter_data *gf);

#endif // WHISPER_PROCESSING_H
