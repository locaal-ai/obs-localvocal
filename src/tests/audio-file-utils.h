
#include <vector>
#include <functional>
#include <string>

std::vector<std::vector<uint8_t>>
read_audio_file(const char *filename, std::function<void(int, int)> initialization_callback);

void write_audio_wav_file(const std::string &filename, const float *pcm32f_data,
			  const size_t frames);
