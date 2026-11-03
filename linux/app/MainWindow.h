/*
FxSound for Linux
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#pragma once

#include "FxController.h"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QVector>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QToolButton;
class QHBoxLayout;
class QGridLayout;
class QWidget;

class SpectrumWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SpectrumWidget(FxController *c, QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    void tick();
    FxController *c_;
    float values_[FxController::NumSpectrumBands] = {};
    float smooth_[FxController::NumSpectrumBands] = {};
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(FxController *c, QWidget *parent = nullptr);

    void showFromTray();
    void toggleVisible();
    void handleCommand(const QString &cmd); // vindo de outra instância (CLI)

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildUi();
    void buildTray();
    void buildEq();
    void refreshPresets();
    void refreshValues();
    void refreshDevices();
    void refreshPower(bool on);
    void refreshModified(bool modified);
    void refreshStatus();
    void showAdvanced();
    void showAbout();
    void savePreset();
    void savePresetAs();
    void deletePreset();
    void importPreset();
    void exportPreset();
    void showMessage(const QString &msg, bool error = false);

    FxController *c_;
    bool updating_ = false;

    QPushButton *power_btn_ = nullptr;
    QComboBox *preset_box_ = nullptr;
    QLabel *modified_lbl_ = nullptr;
    QPushButton *save_btn_ = nullptr;
    QPushButton *revert_btn_ = nullptr;
    QComboBox *device_box_ = nullptr;
    QComboBox *bands_box_ = nullptr;
    QLabel *status_lbl_ = nullptr;
    QWidget *body_ = nullptr;
    QVector<QSlider *> effect_sliders_;
    QVector<QLabel *> effect_values_;

    QWidget *eq_area_ = nullptr;
    QGridLayout *eq_grid_ = nullptr;
    QVector<QSlider *> eq_sliders_;
    QVector<QLabel *> eq_gain_lbls_;
    QVector<QLabel *> eq_freq_lbls_;

    QSystemTrayIcon *tray_ = nullptr;
    QMenu *tray_menu_ = nullptr;
    QMenu *tray_presets_ = nullptr;
    QAction *tray_power_ = nullptr;
    bool tray_hint_shown_ = false;
};
