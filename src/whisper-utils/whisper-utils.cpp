#include "whisper-utils.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"
#include "whisper-processing.h"

#include <obs-module.h>

void shutdown_whisper_thread(struct transcription_filter_data *gf)
{
	obs_log(gf->log_level, "shutdown_whisper_thread");
	if (gf->whisper_context != nullptr) {
		// acquire the mutex before freeing the context
		std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
		whisper_free(gf->whisper_context);
		gf->whisper_context = nullptr;
		gf->wshiper_thread_cv.notify_all();
	}
	if (gf->whisper_thread.joinable()) {
		gf->whisper_thread.join();
	}
	if (!gf->whisper_model_path.empty()) {
		gf->whisper_model_path = "";
	}
}

void start_whisper_thread_with_path(struct transcription_filter_data *gf,
				    const std::string &whisper_model_path,
				    const char *silero_vad_model_file)
{
	obs_log(gf->log_level, "start_whisper_thread_with_path: %s", whisper_model_path.c_str());
	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
	if (gf->whisper_context != nullptr) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_context is not null");
		return;
	}

	// initialize Silero VAD
#ifdef _WIN32
	std::wstring silero_vad_model_path;
	silero_vad_model_path.assign(silero_vad_model_file,
				     silero_vad_model_file + strlen(silero_vad_model_file));
#else
	std::string silero_vad_model_path = silero_vad_model_file;
#endif
	// roughly following https://github.com/SYSTRAN/faster-whisper/blob/master/faster_whisper/vad.py
	// for silero vad parameters
	gf->vad.reset(new VadIterator(silero_vad_model_path, WHISPER_SAMPLE_RATE, 64, 0.5f, 500,
				      200, 250));

	gf->whisper_context = init_whisper_context(whisper_model_path, gf);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_ERROR, "Failed to initialize whisper context");
		return;
	}
	gf->whisper_model_file_currently_loaded = whisper_model_path;
	std::thread new_whisper_thread(whisper_loop, gf);
	gf->whisper_thread.swap(new_whisper_thread);
}

// Finds start of 2-token overlap between two sequences of tokens
// Returns a pair of indices of the first overlapping tokens in the two sequences
// If no overlap is found, the function returns {-1, -1}
// Allows for a single token mismatch in the overlap
std::pair<int, int> findStartOfOverlap(const std::vector<whisper_token_data> &seq1,
				       const std::vector<whisper_token_data> &seq2)
{
	if (seq1.empty() || seq2.empty() || seq1.size() == 1 || seq2.size() == 1) {
		return {-1, -1};
	}
	for (size_t i = seq1.size() - 2; i >= seq1.size() / 2; --i) {
		for (size_t j = 0; j < seq2.size() - 1; ++j) {
			if (seq1[i].id == seq2[j].id) {
				// Check if the next token in both sequences is the same
				if (seq1[i + 1].id == seq2[j + 1].id) {
					return {i, j};
				}
				// 1-skip check on seq1
				if (i + 2 < seq1.size() && seq1[i + 2].id == seq2[j + 1].id) {
					return {i, j};
				}
				// 1-skip check on seq2
				if (j + 2 < seq2.size() && seq1[i + 1].id == seq2[j + 2].id) {
					return {i, j};
				}
			}
		}
	}
	return {-1, -1};
}

// Function to reconstruct a whole sentence from two sentences using overlap info
// If no overlap is found, the function returns the concatenation of the two sequences
std::vector<whisper_token_data> reconstructSentence(const std::vector<whisper_token_data> &seq1,
						    const std::vector<whisper_token_data> &seq2)
{
	auto overlap = findStartOfOverlap(seq1, seq2);
	std::vector<whisper_token_data> reconstructed;

	if (overlap.first == -1 || overlap.second == -1) {
		if (seq1.empty() && seq2.empty()) {
			return reconstructed;
		}
		if (seq1.empty()) {
			return seq2;
		}
		if (seq2.empty()) {
			return seq1;
		}

		// Return concat of seq1 and seq2 if no overlap found
		// check if the last token of seq1 == the first token of seq2
		if (seq1.back().id == seq2.front().id) {
			// don't add the last token of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end() - 1);
			reconstructed.insert(reconstructed.end(), seq2.begin(), seq2.end());
		} else if (seq2.size() > 1ull && seq1.back().id == seq2[1].id) {
			// check if the last token of seq1 == the second token of seq2
			// don't add the last token of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end() - 1);
			// don't add the first token of seq2
			reconstructed.insert(reconstructed.end(), seq2.begin() + 1, seq2.end());
		} else if (seq1.size() > 1ull && seq1[seq1.size() - 2].id == seq2.front().id) {
			// check if the second to last token of seq1 == the first token of seq2
			// don't add the last two tokens of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end() - 2);
			reconstructed.insert(reconstructed.end(), seq2.begin(), seq2.end());
		} else {
			// add all tokens of seq1
			reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.end());
			reconstructed.insert(reconstructed.end(), seq2.begin(), seq2.end());
		}
		return reconstructed;
	}

	// Add tokens from the first sequence up to the overlap
	reconstructed.insert(reconstructed.end(), seq1.begin(), seq1.begin() + overlap.first);

	// Determine the length of the overlap
	size_t overlapLength = 0;
	while (overlap.first + overlapLength < seq1.size() &&
	       overlap.second + overlapLength < seq2.size() &&
	       seq1[overlap.first + overlapLength].id == seq2[overlap.second + overlapLength].id) {
		overlapLength++;
	}

	// Add overlapping tokens
	reconstructed.insert(reconstructed.end(), seq1.begin() + overlap.first,
			     seq1.begin() + overlap.first + overlapLength);

	// Add remaining tokens from the second sequence
	reconstructed.insert(reconstructed.end(), seq2.begin() + overlap.second + overlapLength,
			     seq2.end());

	return reconstructed;
}
