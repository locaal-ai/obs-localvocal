#ifndef WHISPER_PROCESSING_H
#define WHISPER_PROCESSING_H

// buffer size in msec
#define DEFAULT_BUFFER_SIZE_MSEC 2000
// overlap in msec
#define DEFAULT_OVERLAP_SIZE_MSEC 100

void whisper_loop(void *data);
struct whisper_context *init_whisper_context(const std::string &model_path);

#endif // WHISPER_PROCESSING_H
