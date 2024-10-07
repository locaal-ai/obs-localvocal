#ifndef RESAMPLE_UTILS_H
#define RESAMPLE_UTILS_H

#include "transcription-filter-data.h"

int get_data_from_buf_and_resample(transcription_filter_data *gf,
				   uint64_t &start_timestamp_offset_ns,
				   uint64_t &end_timestamp_offset_ns);

#endif
