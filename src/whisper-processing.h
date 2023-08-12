#ifndef WHISPER_PROCESSING_H
#define WHISPER_PROCESSING_H

void whisper_loop(void *data);
struct whisper_context *init_whisper_context(const std::string &model_path);

#endif // WHISPER_PROCESSING_H
