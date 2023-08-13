#ifndef MODEL_DOWNLOADER_H
#define MODEL_DOWNLOADER_H

#include <string>
#include <functional>

bool check_if_model_exists(const std::string &model_name);

// Start the model downloader UI dialog with a callback for when the download is finished
void download_model_with_ui_dialog(
	const std::string &model_name,
	std::function<void(int download_status)> download_finished_callback);

#endif // MODEL_DOWNLOADER_H
