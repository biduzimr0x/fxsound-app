/*
FxSound for Linux
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#include "MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QUrl>
#include <QActionGroup>
#include <QLineEdit>
#include <functional>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

const QColor kAccent(0xe6, 0x34, 0x62);

struct EffectInfo {
    DfxDsp::Effect id;
    const char *label;
    const char *tip;
};

const EffectInfo kEffects[] = {
    {DfxDsp::Fidelity, QT_TRANSLATE_NOOP("MainWindow", "Clareza"),
     QT_TRANSLATE_NOOP("MainWindow", "Fidelity: recupera agudos e definição perdidos na compressão.")},
    {DfxDsp::Ambience, QT_TRANSLATE_NOOP("MainWindow", "Ambiência"),
     QT_TRANSLATE_NOOP("MainWindow", "Ambience: adiciona reverberação e profundidade.")},
    {DfxDsp::Surround, QT_TRANSLATE_NOOP("MainWindow", "Surround 3D"),
     QT_TRANSLATE_NOOP("MainWindow", "Surround: amplia a imagem estéreo.")},
    {DfxDsp::DynamicBoost, QT_TRANSLATE_NOOP("MainWindow", "Reforço dinâmico"),
     QT_TRANSLATE_NOOP("MainWindow", "Dynamic Boost: aumenta o volume percebido sem distorcer.")},
    {DfxDsp::Bass, QT_TRANSLATE_NOOP("MainWindow", "Graves"),
     QT_TRANSLATE_NOOP("MainWindow", "Bass Boost: reforça as baixas frequências.")},
};

QString formatFreq(float hz)
{
    if (hz >= 10000) return QString::number(hz / 1000.0f, 'f', 0) + "k";
    if (hz >= 1000) return QString::number(hz / 1000.0f, 'f', 1) + "k";
    return QString::number(std::lround(hz));
}

QString formatGain(int db) { return db > 0 ? QString("+%1").arg(db) : QString::number(db); }

const char *kStyle = R"(
QMainWindow, QDialog { background: #15151b; }
QWidget { color: #e8e8ee; font-size: 10pt; }
QLabel#title { font-size: 15pt; font-weight: 600; }
QLabel#section { color: #9a9aa8; font-size: 9pt; font-weight: 600; letter-spacing: 1px; }
QLabel#dim { color: #8a8a96; font-size: 8pt; }
QLabel#modified { color: #e63462; font-size: 9pt; }
QFrame#card { background: #1e1e26; border-radius: 10px; }
QComboBox, QPushButton, QToolButton {
    background: #2a2a34; border: 1px solid #34343f; border-radius: 6px; padding: 5px 10px; }
QComboBox:hover, QPushButton:hover, QToolButton:hover { border-color: #e63462; }
QPushButton:disabled { color: #5c5c66; }
QComboBox QAbstractItemView { background: #23232c; selection-background-color: #e63462; }
QPushButton#power { border-radius: 18px; min-width: 36px; max-width: 36px; min-height: 36px; max-height: 36px;
    padding: 0; font-size: 15pt; font-weight: bold; background: #2a2a34; color: #7a7a85; }
QPushButton#power:checked { background: #e63462; color: white; border-color: #e63462; }
QSlider::groove:horizontal { height: 4px; background: #34343f; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #e63462; border-radius: 2px; }
QSlider::handle:horizontal { background: #f2f2f6; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }
QSlider::groove:vertical { width: 4px; background: #34343f; border-radius: 2px; }
QSlider::add-page:vertical { background: #34343f; border-radius: 2px; }
QSlider::handle:vertical { background: #f2f2f6; width: 14px; height: 14px; margin: 0 -5px; border-radius: 7px; }
QWidget#bodyOff QSlider::sub-page:horizontal { background: #55555f; }
QMenu { background: #23232c; border: 1px solid #34343f; }
QMenu::item:selected { background: #e63462; }
QStatusBar { color: #8a8a96; }
)";

QFrame *card(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setObjectName("card");
    return f;
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text.toUpper(), parent);
    l->setObjectName("section");
    return l;
}

} // namespace

// ================================================================ espectro

SpectrumWidget::SpectrumWidget(FxController *c, QWidget *parent) : QWidget(parent), c_(c)
{
    setMinimumHeight(70);
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &SpectrumWidget::tick);
    t->start(33);
}

void SpectrumWidget::tick()
{
    if (!isVisible()) return;
    c_->spectrum(values_, FxController::NumSpectrumBands);
    bool changed = false;
    for (int i = 0; i < FxController::NumSpectrumBands; i++) {
        float target = values_[i];
        float s = smooth_[i];
        // sobe rápido, desce devagar
        s = target > s ? s + (target - s) * 0.6f : s + (target - s) * 0.15f;
        if (std::fabs(s - smooth_[i]) > 1e-4f) changed = true;
        smooth_[i] = s;
    }
    if (changed) update();
}

void SpectrumWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int n = FxController::NumSpectrumBands;
    const int bars_per_band = 5;
    const int total = n * bars_per_band;
    const qreal gap = 3.0;
    const qreal bw = std::max<qreal>(2.0, (width() - gap * (total - 1)) / total);
    const qreal mid = height() / 2.0;

    for (int i = 0; i < total; i++) {
        // interpola entre bandas para um visual contínuo, como o FxVisualizer
        qreal pos = (i + 0.5) / bars_per_band - 0.5;
        int b0 = std::clamp(int(std::floor(pos)), 0, n - 1);
        int b1 = std::clamp(b0 + 1, 0, n - 1);
        qreal t = std::clamp(pos - b0, 0.0, 1.0);
        qreal v = smooth_[b0] * (1 - t) + smooth_[b1] * t;
        qreal h = std::max(3.0, v * (height() - 4));
        QRectF r(i * (bw + gap), mid - h / 2, bw, h);
        QColor col = kAccent;
        col.setAlphaF(0.35 + 0.65 * std::min(1.0, v * 1.4));
        if (!c_->power()) col = QColor(0x44, 0x44, 0x4e);
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawRoundedRect(r, bw / 2, bw / 2);
    }
}

// =========================================================== janela principal

MainWindow::MainWindow(FxController *c, QWidget *parent) : QMainWindow(parent), c_(c)
{
    setWindowTitle("FxSound");
    setWindowIcon(QIcon(":/fxsound.svg"));
    qApp->setStyleSheet(kStyle);
    buildUi();
    buildTray();

    connect(c_, &FxController::powerChanged, this, &MainWindow::refreshPower);
    connect(c_, &FxController::presetsChanged, this, &MainWindow::refreshPresets);
    connect(c_, &FxController::presetSelected, this, [this] { refreshPresets(); refreshValues(); });
    connect(c_, &FxController::modifiedChanged, this, &MainWindow::refreshModified);
    connect(c_, &FxController::devicesChanged, this, [this] { refreshDevices(); refreshStatus(); });
    connect(c_, &FxController::numBandsChanged, this, [this] { buildEq(); refreshValues(); });
    connect(c_, &FxController::errorOccurred, this, [this](const QString &m) { showMessage(m, true); });
    connect(c_, &FxController::message, this, [this](const QString &m) { showMessage(m); });

    auto *status_timer = new QTimer(this);
    connect(status_timer, &QTimer::timeout, this, &MainWindow::refreshStatus);
    status_timer->start(1000);

    refreshPresets();
    refreshValues();
    refreshDevices();
    refreshPower(c_->power());
    refreshModified(c_->isModified());
    refreshStatus();
    resize(760, 640);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(16, 14, 16, 8);
    root->setSpacing(12);

    // ---- cabeçalho: power, título, presets, menu
    auto *header = new QHBoxLayout;
    power_btn_ = new QPushButton(QString::fromUtf8("⏻"), central);
    power_btn_->setObjectName("power");
    power_btn_->setCheckable(true);
    power_btn_->setToolTip(tr("Liga/desliga o FxSound (Ctrl+P)"));
    power_btn_->setShortcut(QKeySequence("Ctrl+P"));
    connect(power_btn_, &QPushButton::clicked, this, [this](bool on) { c_->setPower(on); });
    header->addWidget(power_btn_);

    auto *title = new QLabel("FxSound", central);
    title->setObjectName("title");
    header->addWidget(title);
    header->addSpacing(12);

    auto *prev = new QToolButton(central);
    prev->setText(QString::fromUtf8("‹"));
    prev->setToolTip(tr("Preset anterior (Ctrl+←)"));
    prev->setShortcut(QKeySequence("Ctrl+Left"));
    connect(prev, &QToolButton::clicked, c_, &FxController::previousPreset);
    header->addWidget(prev);

    preset_box_ = new QComboBox(central);
    preset_box_->setMinimumWidth(220);
    connect(preset_box_, &QComboBox::activated, this, [this](int i) {
        int idx = preset_box_->itemData(i).toInt();
        if (idx >= 0) c_->selectPreset(idx);
    });
    header->addWidget(preset_box_, 1);

    auto *next = new QToolButton(central);
    next->setText(QString::fromUtf8("›"));
    next->setToolTip(tr("Próximo preset (Ctrl+→)"));
    next->setShortcut(QKeySequence("Ctrl+Right"));
    connect(next, &QToolButton::clicked, c_, &FxController::nextPreset);
    header->addWidget(next);

    auto *menu_btn = new QToolButton(central);
    menu_btn->setText(QString::fromUtf8("☰"));
    menu_btn->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(menu_btn);
    menu->addAction(tr("Salvar preset"), QKeySequence::Save, this, &MainWindow::savePreset);
    menu->addAction(tr("Salvar como..."), QKeySequence("Ctrl+Shift+S"), this, &MainWindow::savePresetAs);
    menu->addAction(tr("Descartar alterações"), this, [this] { c_->revertPreset(); });
    menu->addAction(tr("Excluir preset"), this, &MainWindow::deletePreset);
    menu->addSeparator();
    menu->addAction(tr("Importar preset..."), this, &MainWindow::importPreset);
    menu->addAction(tr("Exportar preset..."), this, &MainWindow::exportPreset);
    menu->addAction(tr("Abrir pasta de presets"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(c_->userPresetDir()));
    });
    menu->addSeparator();
    menu->addAction(tr("Ajustes avançados..."), QKeySequence("Ctrl+,"), this, &MainWindow::showAdvanced);
    menu->addAction(tr("Sobre"), this, &MainWindow::showAbout);
    menu->addSeparator();
    menu->addAction(tr("Sair"), QKeySequence::Quit, qApp, &QApplication::quit);
    menu_btn->setMenu(menu);
    addActions(menu->actions()); // atalhos funcionam com o menu fechado
    header->addWidget(menu_btn);
    root->addLayout(header);

    // ---- linha de estado do preset + saída
    auto *line2 = new QHBoxLayout;
    modified_lbl_ = new QLabel(tr("● alterado"), central);
    modified_lbl_->setObjectName("modified");
    line2->addWidget(modified_lbl_);
    save_btn_ = new QPushButton(tr("Salvar"), central);
    connect(save_btn_, &QPushButton::clicked, this, &MainWindow::savePreset);
    line2->addWidget(save_btn_);
    revert_btn_ = new QPushButton(tr("Descartar"), central);
    connect(revert_btn_, &QPushButton::clicked, this, [this] { c_->revertPreset(); });
    line2->addWidget(revert_btn_);
    line2->addStretch(1);
    auto *out_lbl = new QLabel(tr("Saída:"), central);
    out_lbl->setObjectName("dim");
    line2->addWidget(out_lbl);
    device_box_ = new QComboBox(central);
    device_box_->setMinimumWidth(260);
    device_box_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(device_box_, &QComboBox::activated, this, [this](int i) {
        c_->setOutputDevice(device_box_->itemData(i).toString());
    });
    line2->addWidget(device_box_, 1);
    root->addLayout(line2);

    body_ = new QWidget(central);
    auto *body = new QVBoxLayout(body_);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);

    // ---- visualizador
    auto *viz_card = card(body_);
    auto *viz_l = new QVBoxLayout(viz_card);
    viz_l->setContentsMargins(14, 10, 14, 10);
    viz_l->addWidget(new SpectrumWidget(c_, viz_card));
    body->addWidget(viz_card);

    // ---- efeitos
    auto *fx_card = card(body_);
    auto *fx_l = new QGridLayout(fx_card);
    fx_l->setContentsMargins(14, 10, 14, 12);
    fx_l->setHorizontalSpacing(14);
    fx_l->addWidget(sectionLabel(tr("Efeitos"), fx_card), 0, 0, 1, 3);
    int row = 1;
    for (const EffectInfo &e : kEffects) {
        auto *lbl = new QLabel(tr(e.label), fx_card);
        lbl->setToolTip(tr(e.tip));
        auto *s = new QSlider(Qt::Horizontal, fx_card);
        s->setRange(0, 10);
        s->setPageStep(1);
        s->setToolTip(tr(e.tip));
        auto *val = new QLabel("0", fx_card);
        val->setMinimumWidth(24);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        DfxDsp::Effect id = e.id;
        connect(s, &QSlider::valueChanged, this, [this, id, val](int v) {
            val->setText(QString::number(v));
            if (!updating_) c_->setEffect(id, float(v));
        });
        fx_l->addWidget(lbl, row, 0);
        fx_l->addWidget(s, row, 1);
        fx_l->addWidget(val, row, 2);
        effect_sliders_.push_back(s);
        effect_values_.push_back(val);
        row++;
    }
    fx_l->setColumnStretch(1, 1);
    body->addWidget(fx_card);

    // ---- equalizador
    auto *eq_card = card(body_);
    auto *eq_l = new QVBoxLayout(eq_card);
    eq_l->setContentsMargins(14, 10, 14, 10);
    auto *eq_head = new QHBoxLayout;
    eq_head->addWidget(sectionLabel(tr("Equalizador"), eq_card));
    eq_head->addStretch(1);
    auto *bl = new QLabel(tr("Bandas:"), eq_card);
    bl->setObjectName("dim");
    eq_head->addWidget(bl);
    bands_box_ = new QComboBox(eq_card);
    for (int n : {5, 10, 15, 20, 31}) bands_box_->addItem(QString::number(n), n);
    connect(bands_box_, &QComboBox::activated, this, [this](int i) {
        c_->setNumBands(bands_box_->itemData(i).toInt());
    });
    eq_head->addWidget(bands_box_);
    auto *zero = new QPushButton(tr("Zerar"), eq_card);
    zero->setToolTip(tr("Coloca todas as bandas em 0 dB"));
    connect(zero, &QPushButton::clicked, this, [this] { c_->resetEq(); refreshValues(); });
    eq_head->addWidget(zero);
    eq_l->addLayout(eq_head);
    eq_area_ = new QWidget(eq_card);
    eq_grid_ = new QGridLayout(eq_area_);
    eq_grid_->setContentsMargins(0, 4, 0, 0);
    eq_grid_->setVerticalSpacing(4);
    eq_l->addWidget(eq_area_, 1);
    body->addWidget(eq_card, 1);

    root->addWidget(body_, 1);
    setCentralWidget(central);

    status_lbl_ = new QLabel(this);
    statusBar()->addWidget(status_lbl_, 1);
    statusBar()->setSizeGripEnabled(false);

    buildEq();
}

void MainWindow::buildEq()
{
    for (QSlider *s : eq_sliders_) s->deleteLater();
    for (QLabel *l : eq_gain_lbls_) l->deleteLater();
    for (QLabel *l : eq_freq_lbls_) l->deleteLater();
    eq_sliders_.clear();
    eq_gain_lbls_.clear();
    eq_freq_lbls_.clear();

    const int n = c_->numBands();
    const int max_gain = int(FxController::MaxEqGain);
    for (int b = 0; b < n; b++) {
        auto *g = new QLabel("0", eq_area_);
        g->setObjectName("dim");
        g->setAlignment(Qt::AlignCenter);
        auto *s = new QSlider(Qt::Vertical, eq_area_);
        s->setRange(-max_gain, max_gain);
        s->setPageStep(1);
        s->setMinimumHeight(110);
        auto *f = new QLabel(formatFreq(c_->bandFrequency(b)), eq_area_);
        f->setObjectName("dim");
        f->setAlignment(Qt::AlignCenter);
        connect(s, &QSlider::valueChanged, this, [this, b, g](int v) {
            g->setText(formatGain(v));
            if (!updating_) c_->setBandGain(b, float(v));
        });
        s->setToolTip(tr("%1 Hz").arg(std::lround(c_->bandFrequency(b))));
        eq_grid_->addWidget(g, 0, b, Qt::AlignHCenter);
        eq_grid_->addWidget(s, 1, b, Qt::AlignHCenter);
        eq_grid_->addWidget(f, 2, b, Qt::AlignHCenter);
        eq_sliders_.push_back(s);
        eq_gain_lbls_.push_back(g);
        eq_freq_lbls_.push_back(f);
    }
    eq_grid_->setRowStretch(1, 1);
    int idx = bands_box_->findData(n);
    if (idx >= 0) bands_box_->setCurrentIndex(idx);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(QIcon(":/fxsound.svg"), this);
    tray_->setToolTip("FxSound");
    tray_menu_ = new QMenu(this);
    tray_menu_->addAction(tr("Mostrar / ocultar"), this, &MainWindow::toggleVisible);
    tray_power_ = tray_menu_->addAction(tr("Ligado"));
    tray_power_->setCheckable(true);
    connect(tray_power_, &QAction::triggered, this, [this](bool on) { c_->setPower(on); });
    tray_presets_ = tray_menu_->addMenu(tr("Presets"));
    tray_menu_->addSeparator();
    tray_menu_->addAction(tr("Sair"), qApp, &QApplication::quit);
    tray_->setContextMenu(tray_menu_);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger) toggleVisible();
        else if (r == QSystemTrayIcon::MiddleClick) c_->setPower(!c_->power());
    });
    tray_->show();
}

// ------------------------------------------------------------- atualizações

void MainWindow::refreshPresets()
{
    updating_ = true;
    preset_box_->clear();
    const auto &ps = c_->presets();
    bool user_header = false;
    for (int i = 0; i < ps.size(); i++) {
        if (ps[i].type == FxController::PresetType::User && !user_header) {
            preset_box_->insertSeparator(preset_box_->count());
            user_header = true;
        }
        QString label = ps[i].name + (ps[i].modified ? QString::fromUtf8(" •") : QString());
        preset_box_->addItem(label, i);
        if (i == c_->currentPreset()) preset_box_->setCurrentIndex(preset_box_->count() - 1);
    }
    updating_ = false;

    if (tray_presets_) {
        tray_presets_->clear();
        auto *group = new QActionGroup(tray_presets_);
        for (int i = 0; i < ps.size(); i++) {
            QAction *a = tray_presets_->addAction(ps[i].name);
            a->setCheckable(true);
            a->setChecked(i == c_->currentPreset());
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, i] { c_->selectPreset(i); });
        }
    }
    if (tray_ && c_->currentPreset() >= 0)
        tray_->setToolTip(QString("FxSound — %1%2")
                              .arg(ps[c_->currentPreset()].name)
                              .arg(c_->power() ? "" : tr(" (desligado)")));
}

void MainWindow::refreshValues()
{
    updating_ = true;
    for (int i = 0; i < effect_sliders_.size(); i++)
        effect_sliders_[i]->setValue(std::lround(c_->effect(kEffects[i].id)));
    if (eq_sliders_.size() != c_->numBands()) buildEq();
    for (int b = 0; b < eq_sliders_.size(); b++) {
        eq_sliders_[b]->setValue(std::lround(c_->bandGain(b)));
        eq_gain_lbls_[b]->setText(formatGain(eq_sliders_[b]->value()));
        eq_freq_lbls_[b]->setText(formatFreq(c_->bandFrequency(b)));
    }
    updating_ = false;
}

void MainWindow::refreshDevices()
{
    QString configured = c_->outputDevice();
    QString active = c_->activeOutputDevice();
    QString active_desc;
    auto devs = c_->devices();
    for (auto &d : devs)
        if (QString::fromStdString(d.name) == active) active_desc = QString::fromStdString(d.description);

    device_box_->clear();
    device_box_->addItem(active_desc.isEmpty() ? tr("Automático (padrão do sistema)")
                                               : tr("Automático — %1").arg(active_desc),
                         QString());
    int sel = 0;
    for (auto &d : devs) {
        device_box_->addItem(QString::fromStdString(d.description), QString::fromStdString(d.name));
        if (QString::fromStdString(d.name) == configured) sel = device_box_->count() - 1;
    }
    if (!configured.isEmpty() && sel == 0) {
        device_box_->addItem(tr("%1 (desconectado)").arg(configured), configured);
        sel = device_box_->count() - 1;
    }
    device_box_->setCurrentIndex(sel);
}

void MainWindow::refreshPower(bool on)
{
    power_btn_->setChecked(on);
    body_->setObjectName(on ? "bodyOn" : "bodyOff");
    body_->setStyleSheet(body_->styleSheet()); // reavalia o seletor
    body_->setEnabled(true);
    if (tray_power_) tray_power_->setChecked(on);
    if (tray_) tray_->setIcon(QIcon(on ? ":/fxsound.svg" : ":/fxsound-off.svg"));
    refreshPresets();
    refreshStatus();
}

void MainWindow::refreshModified(bool modified)
{
    modified_lbl_->setVisible(modified);
    revert_btn_->setVisible(modified);
    save_btn_->setVisible(modified);
    refreshPresets();
}

void MainWindow::refreshStatus()
{
    QString active = c_->activeOutputDevice();
    QString desc = active;
    for (auto &d : c_->devices())
        if (QString::fromStdString(d.name) == active) desc = QString::fromStdString(d.description);
    if (desc.isEmpty()) desc = tr("nenhum dispositivo");
    QString rate = c_->sampleRate() ? QString(" · %1 kHz").arg(c_->sampleRate() / 1000.0, 0, 'g', 3) : QString();
    status_lbl_->setText(c_->power() ? tr("Ligado → %1%2").arg(desc, rate)
                                     : tr("Desligado (áudio direto) → %1").arg(desc));
}

void MainWindow::showMessage(const QString &msg, bool error)
{
    if (error && isVisible()) QMessageBox::warning(this, "FxSound", msg);
    else if (error && tray_) tray_->showMessage("FxSound", msg, QSystemTrayIcon::Warning);
    statusBar()->showMessage(msg, 5000);
}

// ------------------------------------------------------------------ presets

void MainWindow::savePreset()
{
    if (c_->currentPreset() < 0) return;
    if (c_->presets()[c_->currentPreset()].type == FxController::PresetType::Factory) {
        savePresetAs();
        return;
    }
    QString err;
    if (!c_->savePreset(err)) showMessage(err, true);
}

void MainWindow::savePresetAs()
{
    QString current = c_->currentPreset() >= 0 ? c_->presets()[c_->currentPreset()].name : QString();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Salvar preset"), tr("Nome do preset:"), QLineEdit::Normal,
                                         current, &ok);
    if (!ok) return;
    for (auto &p : c_->presets()) {
        if (p.name == name.trimmed() && p.type == FxController::PresetType::User &&
            QMessageBox::question(this, tr("Salvar preset"), tr("Substituir o preset \"%1\"?").arg(p.name)) !=
                QMessageBox::Yes)
            return;
    }
    QString err;
    if (!c_->savePresetAs(name, err)) showMessage(err, true);
}

void MainWindow::deletePreset()
{
    if (c_->currentPreset() < 0) return;
    const auto &p = c_->presets()[c_->currentPreset()];
    if (p.type != FxController::PresetType::User) {
        showMessage(tr("Presets de fábrica não podem ser excluídos."), true);
        return;
    }
    if (QMessageBox::question(this, tr("Excluir preset"), tr("Excluir o preset \"%1\"?").arg(p.name)) !=
        QMessageBox::Yes)
        return;
    QString err;
    if (!c_->deletePreset(err)) showMessage(err, true);
}

void MainWindow::importPreset()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Importar preset"), QDir::homePath(),
                                                tr("Presets do FxSound (*.fac)"));
    if (file.isEmpty()) return;
    QString err;
    if (!c_->importPreset(file, err)) showMessage(err, true);
}

void MainWindow::exportPreset()
{
    if (c_->currentPreset() < 0) return;
    QString name = c_->presets()[c_->currentPreset()].name;
    QString file = QFileDialog::getSaveFileName(this, tr("Exportar preset"), QDir::homePath() + "/" + name + ".fac",
                                                tr("Presets do FxSound (*.fac)"));
    if (file.isEmpty()) return;
    if (!file.endsWith(".fac")) file += ".fac";
    QString err;
    if (!c_->exportPreset(file, err)) showMessage(err, true);
}

// ------------------------------------------------------------ ajustes extra

static QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/fxsound.desktop";
}

void MainWindow::showAdvanced()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Ajustes avançados"));
    auto *form = new QFormLayout(&dlg);

    auto addSlider = [&](const QString &label, float value, float lo, float hi, float step, const QString &unit,
                         std::function<void(float)> apply) {
        auto *row = new QWidget(&dlg);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *s = new QSlider(Qt::Horizontal, row);
        int steps = int(std::lround((hi - lo) / step));
        s->setRange(0, steps);
        s->setValue(int(std::lround((value - lo) / step)));
        auto *v = new QLabel(row);
        v->setMinimumWidth(64);
        auto upd = [=](int i) {
            float val = lo + i * step;
            v->setText(QString::number(val, 'g', 3) + unit);
            apply(val);
        };
        upd(s->value());
        connect(s, &QSlider::valueChanged, &dlg, upd);
        h->addWidget(s, 1);
        h->addWidget(v);
        form->addRow(label, row);
    };

    addSlider(tr("Ganho geral"), c_->masterGain(), -20, 20, 1, " dB", [this](float v) { c_->setMasterGain(v); });
    addSlider(tr("Balanço (E ↔ D)"), c_->balance(), -20, 20, 1, " dB", [this](float v) { c_->setBalance(v); });
    addSlider(tr("Nivelamento de volume"), c_->volumeLeveling(), 0, 4, 0.5f, "",
              [this](float v) { c_->setVolumeLeveling(v); });
    addSlider(tr("Q dos filtros do EQ"), c_->filterQ(), 1, 3, 0.5f, QString::fromUtf8("×"),
              [this](float v) { c_->setFilterQ(v); });

    auto *manage = new QCheckBox(tr("Tornar o FxSound a saída padrão quando ligado"), &dlg);
    manage->setChecked(c_->manageDefaultSink());
    manage->setToolTip(tr("Desmarque para rotear manualmente (ex.: pavucontrol/qpwgraph) "
                          "apenas os apps que devem passar pelo FxSound."));
    connect(manage, &QCheckBox::toggled, this, [this](bool on) { c_->setManageDefaultSink(on); });
    form->addRow(manage);

    auto *wcurve = new QCheckBox(tr("Escala de volume igual à do Windows"), &dlg);
    wcurve->setChecked(c_->windowsVolumeCurve());
    wcurve->setToolTip(tr("Ligado: 30% soa como 30% no Windows (≈ -18 dB). "
                          "Desligado: escala padrão do PipeWire (30% ≈ -31 dB)."));
    connect(wcurve, &QCheckBox::toggled, this, [this](bool on) { c_->setWindowsVolumeCurve(on); });
    form->addRow(wcurve);

    auto *tray_close = new QCheckBox(tr("Fechar a janela mantém o FxSound na bandeja"), &dlg);
    tray_close->setChecked(c_->settings().value("close_to_tray", true).toBool());
    tray_close->setEnabled(tray_ != nullptr);
    connect(tray_close, &QCheckBox::toggled, this, [this](bool on) { c_->settings().setValue("close_to_tray", on); });
    form->addRow(tray_close);

    auto *autostart = new QCheckBox(tr("Iniciar com a sessão (minimizado)"), &dlg);
    autostart->setChecked(QFile::exists(autostartPath()));
    autostart->setToolTip(tr("Cria ~/.config/autostart/fxsound.desktop. No Hyprland, use "
                             "'exec-once = fxsound --minimized' no hyprland.conf."));
    connect(autostart, &QCheckBox::toggled, this, [](bool on) {
        if (!on) { QFile::remove(autostartPath()); return; }
        QDir().mkpath(QFileInfo(autostartPath()).absolutePath());
        QFile f(autostartPath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write("[Desktop Entry]\nType=Application\nName=FxSound\nExec=fxsound --minimized\n"
                    "Icon=fxsound\nX-GNOME-Autostart-enabled=true\n");
    });
    form->addRow(autostart);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
    form->addRow(buttons);
    dlg.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("Sobre o FxSound"),
        tr("<h3>FxSound para Linux %1</h3>"
           "<p>Port não-oficial do <a href='https://github.com/fxsound2/fxsound-app'>FxSound</a> "
           "para Linux/PipeWire. O motor DSP é o original do FxSound, compilado sem alterações "
           "nos algoritmos.</p>"
           "<p>Licença: GNU AGPL v3.</p>"
           "<p>Linha de comando: <code>fxsound --toggle | --on | --off | --next | --prev | --show</code></p>")
            .arg(QApplication::applicationVersion()));
}

// --------------------------------------------------------------- janela/CLI

void MainWindow::showFromTray()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void MainWindow::toggleVisible()
{
    if (isVisible() && !isMinimized()) hide();
    else showFromTray();
}

void MainWindow::handleCommand(const QString &cmd)
{
    if (cmd == "show") showFromTray();
    else if (cmd == "toggle") c_->setPower(!c_->power());
    else if (cmd == "on") c_->setPower(true);
    else if (cmd == "off") c_->setPower(false);
    else if (cmd == "next") c_->nextPreset();
    else if (cmd == "prev") c_->previousPreset();
    else if (cmd.startsWith("preset:")) {
        QString name = cmd.mid(7);
        for (int i = 0; i < c_->presets().size(); i++)
            if (c_->presets()[i].name.compare(name, Qt::CaseInsensitive) == 0) { c_->selectPreset(i); break; }
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (tray_ && tray_->isVisible() && c_->settings().value("close_to_tray", true).toBool()) {
        hide();
        e->ignore();
        if (!tray_hint_shown_) {
            tray_->showMessage("FxSound", tr("O FxSound continua ativo na bandeja."), QSystemTrayIcon::Information,
                               3000);
            tray_hint_shown_ = true;
        }
        return;
    }
    e->accept();
    qApp->quit();
}
