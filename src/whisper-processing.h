#ifndef WHISPER_PROCESSING_H
#define WHISPER_PROCESSING_H

// buffer size in msec
#define BUFFER_SIZE_MSEC 3000
// at 16Khz, BUFFER_SIZE_MSEC is WHISPER_FRAME_SIZE samples
#define WHISPER_FRAME_SIZE 48000
// overlap in msec
#define OVERLAP_SIZE_MSEC 100

void whisper_loop(void *data);
struct whisper_context *init_whisper_context(const std::string &model_path);

#endif // WHISPER_PROCESSING_H
