#ifndef FILTERREPLACEDIALOG_H
#define FILTERREPLACEDIALOG_H

#include <QDialog>
#include <QTableWidgetItem>

#include "transcription-filter-data.h"

namespace Ui {
class FilterReplaceDialog;
}

class FilterReplaceDialog : public QDialog {
	Q_OBJECT

public:
	explicit FilterReplaceDialog(QWidget *parent, transcription_filter_data *ctx_);
	~FilterReplaceDialog();

private:
	Ui::FilterReplaceDialog *ui;
	transcription_filter_data *ctx;

private slots:
	void addFilter();
	void removeFilter();
	void editFilter(QTableWidgetItem *item);
	void addPrepopulatedFilter();
};

#endif // FILTERREPLACEDIALOG_H
