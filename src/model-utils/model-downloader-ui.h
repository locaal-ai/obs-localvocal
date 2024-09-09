#ifndef MODEL_DOWNLOADER_UI_H
#define MODEL_DOWNLOADER_UI_H

#include <QtWidgets>
#include <QThread>

#include <string>
#include <functional>

#include <curl/curl.h>

#include "model-downloader-types.h"

class ModelDownloadWorker : public QObject {
	Q_OBJECT
public:
	ModelDownloadWorker(const ModelInfo &model_info_);
	~ModelDownloadWorker();

public slots:
	void download_model();

signals:
	void download_progress(int progress);
	void download_finished(const std::string &path);
	void download_error(const std::string &reason);

private:
	static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
				     curl_off_t ultotal, curl_off_t ulnow);
	ModelInfo model_info;
};

class ModelDownloader : public QDialog {
	Q_OBJECT
public:
	ModelDownloader(const ModelInfo &model_info,
			download_finished_callback_t download_finished_callback,
			QWidget *parent = nullptr);
	~ModelDownloader();

public slots:
	void update_progress(int progress);
	void download_finished(const std::string &path);
	void show_error(const std::string &reason);

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	QVBoxLayout *layout;
	QProgressBar *progress_bar;
	QPointer<QThread> download_thread;
	QPointer<ModelDownloadWorker> download_worker;
	// Callback for when the download is finished
	download_finished_callback_t download_finished_callback;
	bool mPrepareToClose;
	void close();
};

#endif // MODEL_DOWNLOADER_UI_H
