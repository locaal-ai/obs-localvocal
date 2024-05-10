#ifndef MODEL_DOWNLOADER_H
#define MODEL_DOWNLOADER_H

#include <string>

#include "model-downloader-types.h"

std::string find_model_folder(const ModelInfo &model_info);
std::string find_model_bin_file(const ModelInfo &model_info);

// Start the model downloader UI dialog with a callback for when the download is finished
void download_model_with_ui_dialog(const ModelInfo &model_info,
				   download_finished_callback_t download_finished_callback);

#endif // MODEL_DOWNLOADER_H
