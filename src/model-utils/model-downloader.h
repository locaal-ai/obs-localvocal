#ifndef MODEL_DOWNLOADER_H
#define MODEL_DOWNLOADER_H

#include <string>
#include <functional>

#include "model-downloader-types.h"

std::string find_model_file(const std::string &model_name);

// Start the model downloader UI dialog with a callback for when the download is finished
void download_model_with_ui_dialog(const std::string &model_name,
				   download_finished_callback_t download_finished_callback);

#endif // MODEL_DOWNLOADER_H
