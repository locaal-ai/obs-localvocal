#ifndef WHISPER_PROCESSING_H
#define WHISPER_PROCESSING_H

// buffer size in msec
#define BUFFER_SIZE_MSEC 3000
// at 16Khz, 3000 msec is 48000 samples
#define WHISPER_FRAME_SIZE 48000
// overlap in msec
#define OVERLAP_SIZE_MSEC 340

void whisper_loop(void *data);
struct whisper_context *init_whisper_context(const std::string &model_path);

#endif // WHISPER_PROCESSING_H
