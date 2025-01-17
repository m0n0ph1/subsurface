#ifndef CONFIGUREDIVECOMPUTERDIALOG_H
#define CONFIGUREDIVECOMPUTERDIALOG_H

#include <QDialog>
#include <QStringListModel>
#include "ui_configuredivecomputerdialog.h"
#include "../libdivecomputer.h"
#include "configuredivecomputer.h"

namespace Ui {
class ConfigureDiveComputerDialog;
}

class ConfigureDiveComputerDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ConfigureDiveComputerDialog(QWidget *parent = 0);
	~ConfigureDiveComputerDialog();

private slots:
	void readSettings();
	void resetSettings();
	void configMessage(QString msg);
	void configError(QString err);
	void on_cancel_clicked();
	void deviceReadFinished();
	void on_saveSettingsPushButton_clicked();
	void deviceDetailsReceived(DeviceDetails *newDeviceDetails);
	void reloadValues();
	void on_backupButton_clicked();

	void on_restoreBackupButton_clicked();


	void on_updateFirmwareButton_clicked();

	void on_DiveComputerList_currentRowChanged(int currentRow);

private:
	Ui::ConfigureDiveComputerDialog ui;

	QStringList vendorList;
	QHash<QString, QStringList> productList;

	ConfigureDiveComputer *config;
	device_data_t device_data;
	void getDeviceData();

	QHash<QString, dc_descriptor_t *> descriptorLookup;
	void fill_device_list(int dc_type);
	void fill_computer_list();

	DeviceDetails *deviceDetails;
	void populateDeviceDetails();
	void populateDeviceDetailsOSTC3();
	void populateDeviceDetailsSuuntoVyper();
	void reloadValuesOSTC3();
	void reloadValuesSuuntoVyper();

	QString selected_vendor;
	QString selected_product;
};

#endif // CONFIGUREDIVECOMPUTERDIALOG_H
