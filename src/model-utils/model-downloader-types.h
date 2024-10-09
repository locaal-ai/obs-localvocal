#ifndef MODEL_DOWNLOADER_TYPES_H
#define MODEL_DOWNLOADER_TYPES_H

#include <functional>
#include <map>
#include <string>
#include <vector>

typedef std::function<void(int download_status, const std::string &path)>
	download_finished_callback_t;

struct ModelFileDownloadInfo {
	std::string url;
	std::string sha256;
};

enum ModelType { MODEL_TYPE_TRANSCRIPTION, MODEL_TYPE_TRANSLATION };

struct ExtraInfo {
	std::string language;
	std::string description;
	std::string source;
};

struct ModelInfo {
	std::string friendly_name;
	std::string local_folder_name;
	ModelType type;
	std::vector<ModelFileDownloadInfo> files;
	ExtraInfo extra;
};

extern const std::map<std::string, ModelInfo> &models_info();
extern const std::vector<ModelInfo> get_sorted_models_info();

#endif /* MODEL_DOWNLOADER_TYPES_H */
