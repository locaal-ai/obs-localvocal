#ifndef TOKEN_BUFFER_THREAD_H
#define TOKEN_BUFFER_THREAD_H

#include <queue>
#include <vector>

#include <whisper.h>

struct transcription_filter_data;

class TokenBuffer {
public:
	TokenBuffer() = default;
	~TokenBuffer();
	void initialize(struct transcription_filter_data *gf);
	void addTokens(const std::vector<whisper_token_data> &tokens);
private:
	struct transcription_filter_data *gf;
	std::deque<whisper_token_data> wordQueue;
};

#endif
