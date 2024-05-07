#ifndef MODEL_FIND_UTILS_H
#define MODEL_FIND_UTILS_H

#include <string>

#include "model-downloader-types.h"

std::string find_file_in_folder_by_name(const std::string &folder_path,
					const std::string &file_name);
std::string find_bin_file_in_folder(const std::string &path);

#endif // MODEL_FIND_UTILS_H
