#include "model-downloader-ui.h"
#include "plugin-support.h"

#include <obs-module.h>

#include <filesystem>

const std::string MODEL_BASE_PATH = "https://huggingface.co/ggerganov/whisper.cpp";
const std::string MODEL_PREFIX = "resolve/main/";

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}

ModelDownloader::ModelDownloader(const std::string &model_name,
				 download_finished_callback_t download_finished_callback_,
				 QWidget *parent)
	: QDialog(parent), download_finished_callback(download_finished_callback_)
{
	this->setWindowTitle("LocalVocal: Downloading model...");
	this->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
	this->setFixedSize(300, 100);
	// Bring the dialog to the front
	this->activateWindow();
	this->raise();

	this->layout = new QVBoxLayout(this);

	// Add a label for the model name
	QLabel *model_name_label = new QLabel(this);
	model_name_label->setText(QString::fromStdString(model_name));
	model_name_label->setAlignment(Qt::AlignCenter);
	this->layout->addWidget(model_name_label);

	this->progress_bar = new QProgressBar(this);
	this->progress_bar->setRange(0, 100);
	this->progress_bar->setValue(0);
	this->progress_bar->setAlignment(Qt::AlignCenter);
	// Show progress as a percentage
	this->progress_bar->setFormat("%p%");
	this->layout->addWidget(this->progress_bar);

	this->download_thread = new QThread();
	this->download_worker = new ModelDownloadWorker(model_name);
	this->download_worker->moveToThread(this->download_thread);

	connect(this->download_thread, &QThread::started, this->download_worker,
		&ModelDownloadWorker::download_model);
	connect(this->download_worker, &ModelDownloadWorker::download_progress, this,
		&ModelDownloader::update_progress);
	connect(this->download_worker, &ModelDownloadWorker::download_finished, this,
		&ModelDownloader::download_finished);
	connect(this->download_worker, &ModelDownloadWorker::download_finished,
		this->download_thread, &QThread::quit);
	connect(this->download_worker, &ModelDownloadWorker::download_finished,
		this->download_worker, &ModelDownloadWorker::deleteLater);
	connect(this->download_worker, &ModelDownloadWorker::download_error, this,
		&ModelDownloader::show_error);
	connect(this->download_thread, &QThread::finished, this->download_thread,
		&QThread::deleteLater);

	this->download_thread->start();
}

void ModelDownloader::closeEvent(QCloseEvent *e)
{
	if (!this->mPrepareToClose)
		e->ignore();
	else
		QDialog::closeEvent(e);
}

void ModelDownloader::close()
{
	this->mPrepareToClose = true;

	QDialog::close();
}

void ModelDownloader::update_progress(int progress)
{
	this->progress_bar->setValue(progress);
}

void ModelDownloader::download_finished(const std::string &path)
{
	// Call the callback with the path to the downloaded model
	this->download_finished_callback(0, path);
	// Close the dialog
	this->close();
}

void ModelDownloader::show_error(const std::string &reason)
{
	this->setWindowTitle("Download failed!");
	this->progress_bar->setFormat("Download failed!");
	this->progress_bar->setAlignment(Qt::AlignCenter);
	this->progress_bar->setStyleSheet("QProgressBar::chunk { background-color: #FF0000; }");
	// Add a label to show the error
	QLabel *error_label = new QLabel(this);
	error_label->setText(QString::fromStdString(reason));
	error_label->setAlignment(Qt::AlignCenter);
	// Color red
	error_label->setStyleSheet("QLabel { color : red; }");
	this->layout->addWidget(error_label);
	// Add a button to close the dialog
	QPushButton *close_button = new QPushButton("Close", this);
	this->layout->addWidget(close_button);
	connect(close_button, &QPushButton::clicked, this, &ModelDownloader::close);
	this->download_finished_callback(1, "");
}

ModelDownloadWorker::ModelDownloadWorker(const std::string &model_name_)
{
	this->model_name = model_name_;
}

void ModelDownloadWorker::download_model()
{
	char *module_config_path = obs_module_get_config_path(obs_current_module(), "models");
	// Check if the config folder exists
	if (!std::filesystem::exists(module_config_path)) {
		obs_log(LOG_WARNING, "Config folder does not exist: %s", module_config_path);
		// Create the config folder
		if (!std::filesystem::create_directories(module_config_path)) {
			obs_log(LOG_ERROR, "Failed to create config folder: %s",
				module_config_path);
			emit download_error("Failed to create config folder.");
			return;
		}
	}

	char *model_save_path_str =
		obs_module_get_config_path(obs_current_module(), this->model_name.c_str());
	std::string model_save_path(model_save_path_str);
	bfree(model_save_path_str);
	obs_log(LOG_INFO, "Model save path: %s", model_save_path.c_str());

	// extract filename from path in this->modle_name
	const std::string model_filename =
		this->model_name.substr(this->model_name.find_last_of("/\\") + 1);

	std::string model_url = MODEL_BASE_PATH + "/" + MODEL_PREFIX + model_filename;
	obs_log(LOG_INFO, "Model URL: %s", model_url.c_str());

	CURL *curl = curl_easy_init();
	if (curl) {
		FILE *fp = fopen(model_save_path.c_str(), "wb");
		if (fp == nullptr) {
			obs_log(LOG_ERROR, "Failed to open file %s.", model_save_path.c_str());
			emit download_error("Failed to open file.");
			return;
		}
		curl_easy_setopt(curl, CURLOPT_URL, model_url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
				 ModelDownloadWorker::progress_callback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
		// Follow redirects
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			obs_log(LOG_ERROR, "Failed to download model %s.",
				this->model_name.c_str());
			emit download_error("Failed to download model.");
		}
		curl_easy_cleanup(curl);
		fclose(fp);
		emit download_finished(model_save_path);
	} else {
		obs_log(LOG_ERROR, "Failed to initialize curl.");
		emit download_error("Failed to initialize curl.");
	}
}

int ModelDownloadWorker::progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
					   curl_off_t, curl_off_t)
{
	if (dltotal == 0) {
		return 0; // Unknown progress
	}
	ModelDownloadWorker *worker = (ModelDownloadWorker *)clientp;
	if (worker == nullptr) {
		obs_log(LOG_ERROR, "Worker is null.");
		return 1;
	}
	int progress = (int)(dlnow * 100l / dltotal);
	emit worker->download_progress(progress);
	return 0;
}

ModelDownloader::~ModelDownloader()
{
	if (this->download_thread != nullptr) {
		if (this->download_thread->isRunning()) {
			this->download_thread->quit();
			this->download_thread->wait();
		}
		delete this->download_thread;
	}
	delete this->download_worker;
}

ModelDownloadWorker::~ModelDownloadWorker()
{
	// Do nothing
}
