#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <obs-module.h>

#include "model-find-utils.h"
#include "plugin-support.h"

std::string find_file_in_folder_by_name(const std::string &folder_path,
					const std::string &file_name)
{
	for (const auto &entry : std::filesystem::directory_iterator(folder_path)) {
		if (entry.path().filename() == file_name) {
			return entry.path().string();
		}
	}
	return "";
}

std::string find_bin_file_in_folder(const std::string &model_local_folder_path)
{
	// find .bin file in folder
	for (const auto &entry : std::filesystem::directory_iterator(model_local_folder_path)) {
		if (entry.path().extension() == ".bin") {
			const std::string bin_file_path = entry.path().string();
			obs_log(LOG_INFO, "Model bin file found in folder: %s",
				bin_file_path.c_str());
			return bin_file_path;
		}
	}
	obs_log(LOG_ERROR, "Model bin file not found in folder: %s",
		model_local_folder_path.c_str());
	return "";
}
