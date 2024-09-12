#include "filter-replace-dialog.h"
#include "ui_filter-replace-dialog.h"

FilterReplaceDialog::FilterReplaceDialog(QWidget *parent, transcription_filter_data *ctx_)
	: QDialog(parent),
	  ctx(ctx_),
	  ui(new Ui::FilterReplaceDialog)
{
	ui->setupUi(this);

	// populate the tableWidget with the filter_words_replace map
	ui->tableWidget->setRowCount((int)ctx->filter_words_replace.size());
	for (size_t i = 0; i < ctx->filter_words_replace.size(); i++) {
		const std::string key = std::get<0>(ctx->filter_words_replace[i]);
		const std::string value = std::get<1>(ctx->filter_words_replace[i]);
		ui->tableWidget->setItem((int)i, 0,
					 new QTableWidgetItem(QString::fromStdString(key)));
		ui->tableWidget->setItem((int)i, 1,
					 new QTableWidgetItem(QString::fromStdString(value)));
	}

	// connect toolButton_add
	connect(ui->toolButton_add, &QToolButton::clicked, this, &FilterReplaceDialog::addFilter);
	// connect toolButton_remove
	connect(ui->toolButton_remove, &QToolButton::clicked, this,
		&FilterReplaceDialog::removeFilter);
	// connect edit triggers
	connect(ui->tableWidget, &QTableWidget::itemChanged, this,
		&FilterReplaceDialog::editFilter);
	// connect toolButton_addPrepopulatedFilter
	connect(ui->toolButton_addPrepopulatedFilter, &QToolButton::clicked, this,
		&FilterReplaceDialog::addPrepopulatedFilter);
}

FilterReplaceDialog::~FilterReplaceDialog()
{
	delete ui;
}

void FilterReplaceDialog::addFilter()
{
	ui->tableWidget->insertRow(ui->tableWidget->rowCount());
	// add an empty filter_words_replace map entry
	ctx->filter_words_replace.push_back(std::make_tuple("", ""));
}

void FilterReplaceDialog::removeFilter()
{
	if (ui->tableWidget->currentRow() == -1) {
		return;
	}
	ui->tableWidget->removeRow(ui->tableWidget->currentRow());
	// remove the filter_words_replace map entry
	ctx->filter_words_replace.erase(ctx->filter_words_replace.begin() +
					ui->tableWidget->currentRow() + 1);
}

void FilterReplaceDialog::editFilter(QTableWidgetItem *item)
{
	if (item->row() >= (int)ctx->filter_words_replace.size()) {
		return;
	}

	std::string key;
	if (ui->tableWidget->item(item->row(), 0) == nullptr) {
		key = "";
	} else {
		key = ui->tableWidget->item(item->row(), 0)->text().toStdString();
	}
	std::string value;
	if (ui->tableWidget->item(item->row(), 1) == nullptr) {
		value = "";
	} else {
		value = ui->tableWidget->item(item->row(), 1)->text().toStdString();
	}
	// use the row number to update the filter_words_replace map
	ctx->filter_words_replace[item->row()] = std::make_tuple(key, value);
}

void FilterReplaceDialog::addPrepopulatedFilter()
{
	// add a prepopulated filter_words_replace map entry
	// check the value of the comboBox_selectPrepopulatedFilter
	// and add the corresponding filter_words_replace map entry
	std::string replace_value = "";
	std::string replace_pattern;
	const std::string selected =
		ui->comboBox_selectPrepopulatedFilter->currentText().toStdString();
	if (selected == "English Swear Words") {
		replace_pattern = "(fuck|shit|bitch|cunt|cock|dick|pussy)";
		replace_value = "****";
	} else if (selected == "English Hallucinations") {
		replace_pattern = "(Thank you|Thanks for watching|Please subscribe)";
	} else if (selected == "Korean Hallucinations") {
		replace_pattern = "MBC.*";
	}
	ctx->filter_words_replace.push_back(std::make_tuple(replace_pattern, replace_value));
	ui->tableWidget->insertRow(ui->tableWidget->rowCount());
	ui->tableWidget->setItem(ui->tableWidget->rowCount() - 1, 0,
				 new QTableWidgetItem(QString::fromStdString(replace_pattern)));
	ui->tableWidget->setItem(ui->tableWidget->rowCount() - 1, 1,
				 new QTableWidgetItem(QString::fromStdString(replace_value)));
}
