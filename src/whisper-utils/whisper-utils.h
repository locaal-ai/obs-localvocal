/**
 * @file whisper-utils.h
 * @brief Utility functions for handling whisper transcription operations.
 *
 * This header file contains declarations for various utility functions used
 * in the whisper transcription process, including thread management, sequence
 * operations, and timestamp formatting.
 *
 * @note The timestamp conversion function is adapted from the whisper.cpp project.
 *
 * @see transcription-filter-data.h
 */
#ifndef WHISPER_UTILS_H
#define WHISPER_UTILS_H

#include "transcription-filter-data.h"

#include <string>
#include <vector>

/**
 * @brief Shuts down the whisper thread.
 *
 * This function terminates the whisper thread associated with the given
 * transcription filter data.
 *
 * @param gf Pointer to the transcription filter data structure.
 */
void shutdown_whisper_thread(struct transcription_filter_data *gf);

/**
 * @brief Starts the whisper thread with a specified path.
 *
 * This function initializes and starts the whisper thread using the provided
 * transcription filter data, path, and Silero VAD model file.
 *
 * @param gf Pointer to the transcription filter data structure.
 * @param path Reference to a string containing the path.
 * @param silero_vad_model_file Pointer to a character array containing the Silero VAD model file.
 */
void start_whisper_thread_with_path(struct transcription_filter_data *gf, const std::string &path,
				    const char *silero_vad_model_file);

/**
 * @brief Finds the start of overlap between two sequences.
 *
 * This function compares two sequences of whisper token data and determines
 * the starting indices of their overlap.
 *
 * @param seq1 Reference to the first sequence of whisper token data.
 * @param seq2 Reference to the second sequence of whisper token data.
 * @return std::pair<int, int> A pair of integers representing the starting indices of the overlap in seq1 and seq2.
 */
std::pair<int, int> findStartOfOverlap(const std::vector<whisper_token_data> &seq1,
				       const std::vector<whisper_token_data> &seq2);

/**
 * @brief Reconstructs a sentence from two sequences.
 *
 * This function merges two sequences of whisper token data to reconstruct a
 * complete sentence.
 *
 * @param seq1 Reference to the first sequence of whisper token data.
 * @param seq2 Reference to the second sequence of whisper token data.
 * @return std::vector<whisper_token_data> A vector containing the reconstructed sentence.
 */
std::vector<whisper_token_data> reconstructSentence(const std::vector<whisper_token_data> &seq1,
						    const std::vector<whisper_token_data> &seq2);

/**
 * @brief Converts a timestamp in milliseconds to a string in the format "MM:SS.sss".
 *
 * This function takes a timestamp in milliseconds and converts it to a string
 * representation in the format "MM:SS.sss".
 *
 * @param t_ms_offset Timestamp in milliseconds (offset from the beginning of the stream).
 * @return std::string Timestamp in the format "MM:SS.sss".
 */
std::string to_timestamp(uint64_t t_ms_offset);

#endif // WHISPER_UTILS_H
