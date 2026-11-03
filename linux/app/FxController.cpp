/*
FxSound for Linux
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#include "FxController.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

#ifndef FXSOUND_PRESET_DIR
#define FXSOUND_PRESET_DIR "/usr/share/fxsound/presets"
#endif

namespace {

QString factoryPresetDir()
{
    QByteArray env = qgetenv("FXSOUND_PRESET_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
#ifdef FXSOUND_SOURCE_PRESET_DIR
    // execução a partir da árvore de build (desenvolvimento)
    if (!QDir(FXSOUND_PRESET_DIR).exists()) return QStringLiteral(FXSOUND_SOURCE_PRESET_DIR);
#endif
    return QStringLiteral(FXSOUND_PRESET_DIR);
}

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/fxsound";
}

bool validPresetName(const QString &name, QString &error)
{
    QString n = name.trimmed();
    if (n.isEmpty()) { error = QObject::tr("O nome do preset não pode ser vazio."); return false; }
    if (n.contains('/') || n.contains('\\') || n.startsWith('.')) {
        error = QObject::tr("O nome do preset não pode conter '/', '\\' nem começar com '.'.");
        return false;
    }
    if (n.size() > 60) { error = QObject::tr("Nome muito longo (máximo 60 caracteres)."); return false; }
    return true;
}

} // namespace

FxController::FxController(QObject *parent)
    : QObject(parent), settings_(QStringLiteral("fxsound"), QStringLiteral("fxsound"))
{
    autosave_timer_.setInterval(5000);
    connect(&autosave_timer_, &QTimer::timeout, this, [this] { if (dirty_) autoSave(); });

    engine_.onDevicesChanged = [this] {
        QMetaObject::invokeMethod(this, [this] { emit devicesChanged(); }, Qt::QueuedConnection);
    };
    engine_.onError = [this](const std::string &e) {
        QString msg = QString::fromStdString(e);
        QMetaObject::invokeMethod(this, [this, msg] { emit errorOccurred(msg); }, Qt::QueuedConnection);
    };
}

FxController::~FxController() { shutdown(); }

bool FxController::init(QString &error)
{
    QDir().mkpath(userPresetDir());
    QDir().mkpath(dataDir() + "/autosave");

    int nb = settings_.value("num_bands", 10).toInt();
    if (nb != 5 && nb != 10 && nb != 15 && nb != 20 && nb != 31) nb = 10;
    engine_.withDsp([&](DfxDsp &d) { d.setNumBands(nb); return 0; });

    scanPresets();
    if (presets_.isEmpty()) {
        error = tr("Nenhum preset encontrado em %1.").arg(factoryPresetDir());
        return false;
    }

    QString wanted = settings_.value("preset", "General").toString();
    int idx = 0;
    for (int i = 0; i < presets_.size(); i++)
        if (presets_[i].name == wanted) { idx = i; break; }
    selectPreset(idx);

    engine_.setManageDefaultSink(settings_.value("manage_default_sink", true).toBool());
    engine_.setWindowsVolumeCurve(windowsVolumeCurve());

    std::string err;
    if (!engine_.start(err)) {
        error = QString::fromStdString(err);
        return false;
    }
    engine_.setOutputDevice(settings_.value("output_device").toString().toStdString());
    // abrir o FxSound sempre liga o processamento; fechar devolve o áudio normal
    setPower(true);
    autosave_timer_.start();
    return true;
}

void FxController::shutdown()
{
    if (shut_down_) return;
    shut_down_ = true;
    moveVolumeToDevice();
    autosave_timer_.stop();
    if (dirty_) autoSave();
    settings_.sync();
    engine_.stop();
}

// ------------------------------------------------------------------- power

// ------------------------------------------------- volume único (como no Windows)
//
// O FxSound do Windows mantém um único volume: o do sistema. Aqui, ao ligar,
// o sink FxSound herda o volume do dispositivo real, que vai para 100%; ao
// desligar/sair, o volume volta para o dispositivo. Usa o pactl
// (pipewire-pulse), que aceita nomes de nó e valores brutos sem perda.

namespace {

int pactlGetVolume(const QString &sink)
{
    QProcess p;
    p.start("pactl", {"get-sink-volume", sink});
    if (!p.waitForFinished(2000) || p.exitCode() != 0) return -1;
    auto m = QRegularExpression(R"(:\s*(\d+)\s*/)").match(QString::fromUtf8(p.readAllStandardOutput()));
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

bool pactlSetVolume(const QString &sink, int raw)
{
    QProcess p;
    p.start("pactl", {"set-sink-volume", sink, QString::number(raw)});
    return p.waitForFinished(2000) && p.exitCode() == 0;
}

constexpr int kPaVolumeNorm = 65536; // 100%

} // namespace

void FxController::moveVolumeToFx(int attempt)
{
    if (!power_ || !manageDefaultSink() || !volume_device_.isEmpty() || shut_down_) return;
    const QString dev = activeOutputDevice();
    const QString fx = FxEngine::DefaultSinkName;
    int dev_vol = dev.isEmpty() ? -1 : pactlGetVolume(dev);
    int fx_vol = pactlGetVolume(fx);
    if (dev_vol < 0 || fx_vol < 0) {
        // o sink ainda está sendo criado (início do app): tenta de novo
        if (attempt < 20) QTimer::singleShot(250, this, [this, attempt] { moveVolumeToFx(attempt + 1); });
        return;
    }
    if (dev_vol == kPaVolumeNorm) return; // dispositivo já em 100%: nada a transferir
    pactlSetVolume(fx, dev_vol);
    pactlSetVolume(dev, kPaVolumeNorm);
    volume_device_ = dev;
    settings_.setValue("volume_device", dev);
}

void FxController::moveVolumeToDevice()
{
    QString dev = volume_device_;
    if (dev.isEmpty()) dev = settings_.value("volume_device").toString();
    if (dev.isEmpty()) return;
    int fx_vol = pactlGetVolume(FxEngine::DefaultSinkName);
    if (fx_vol >= 0) pactlSetVolume(dev, fx_vol);
    volume_device_.clear();
    settings_.remove("volume_device");
}

void FxController::setPower(bool on)
{
    bool was_on = power_;
    if (was_on && !on) moveVolumeToDevice();
    power_ = on;
    engine_.setPower(on);
    if (on && !was_on) moveVolumeToFx();
    settings_.setValue("power", on);
    emit powerChanged(on);
}

// ----------------------------------------------------------------- presets

QString FxController::userPresetDir() const { return dataDir() + "/presets"; }

QString FxController::autoSavePath(const QString &preset_name) const
{
    return dataDir() + "/autosave/" + preset_name + ".fac";
}

void FxController::scanPresets()
{
    QString selected = (current_ >= 0 && current_ < presets_.size()) ? presets_[current_].name : QString();
    presets_.clear();

    auto scan = [&](const QString &dir, PresetType type) {
        QDir d(dir);
        QStringList files = d.entryList({"*.fac"}, QDir::Files);
        QCollator coll; coll.setNumericMode(true);
        std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) { return coll.compare(a, b) < 0; });
        QVector<Preset> found;
        for (const QString &f : files) {
            QString path = d.absoluteFilePath(f);
            DfxPreset info = engine_.withDsp([&](DfxDsp &dsp) { return dsp.getPresetInfo(w(path)); });
            QString name = QString::fromStdWString(info.name).trimmed();
            if (name.isEmpty()) continue;
            found.push_back({name, path, type, QFile::exists(autoSavePath(name))});
        }
        if (type == PresetType::User)
            std::sort(found.begin(), found.end(), [&](auto &a, auto &b) { return coll.compare(a.name, b.name) < 0; });
        presets_ += found;
    };
    scan(factoryPresetDir(), PresetType::Factory);
    scan(userPresetDir(), PresetType::User);

    current_ = -1;
    for (int i = 0; i < presets_.size(); i++)
        if (presets_[i].name == selected) current_ = i;
    emit presetsChanged();
}

void FxController::syncDspAfterLoad()
{
    // Mesma sequência do FxController do Windows após loadPreset(): os
    // valores de efeito são lidos em 0..1 e reaplicados em 0..10 para
    // sincronizar botões/knobs internos do DSP; idem para as bandas do EQ.
    engine_.withDsp([](DfxDsp &d) {
        for (int e = 0; e < DfxDsp::NumEffects; e++)
            d.setEffectValue((DfxDsp::Effect)e, d.getEffectValue((DfxDsp::Effect)e) * 10);
        for (int b = 0; b < d.getNumEqBands(); b++) {
            d.setEqBandFrequency(b, d.getEqBandFrequency(b));
            d.setEqBandBoostCut(b, d.getEqBandBoostCut(b));
        }
        return 0;
    });
    applyAdvancedSettings();
}

void FxController::applyAdvancedSettings()
{
    float mg = settings_.value("master_gain", 0.0).toFloat();
    float bl = settings_.value("balance", 0.0).toFloat();
    float vl = settings_.value("volume_leveling", 0.0).toFloat();
    float fq = settings_.value("filter_q", 1.0).toFloat();
    if (mg < -20 || mg > 20) mg = 0;
    if (bl < -20 || bl > 20) bl = 0;
    if (vl < 0 || vl > 4) vl = 0;
    if (fq < 1 || fq > 3) fq = 1;
    engine_.withDsp([&](DfxDsp &d) {
        d.setMasterGain(mg); d.setBalance(bl); d.setVolumeLeveling(vl); d.setFilterQ(fq);
        return 0;
    });
}

void FxController::selectPreset(int index)
{
    if (index < 0 || index >= presets_.size()) return;
    if (dirty_ && current_ >= 0 && current_ != index) autoSave();

    Preset &p = presets_[index];
    QString autosave = autoSavePath(p.name);
    bool has_autosave = QFile::exists(autosave);
    int r = engine_.withDsp([&](DfxDsp &d) { return d.loadPreset(w(has_autosave ? autosave : p.path)); });
    if (r != 0 && has_autosave) {
        // autosave corrompido: volta ao original
        QFile::remove(autosave);
        has_autosave = false;
        r = engine_.withDsp([&](DfxDsp &d) { return d.loadPreset(w(p.path)); });
    }
    if (r != 0) {
        emit errorOccurred(tr("Não foi possível carregar o preset \"%1\".").arg(p.name));
        return;
    }
    p.modified = has_autosave;
    current_ = index;
    dirty_ = false;
    syncDspAfterLoad();
    settings_.setValue("preset", p.name);
    emit presetSelected(index);
    emit modifiedChanged(p.modified);
}

void FxController::nextPreset()
{
    if (!presets_.isEmpty()) selectPreset((current_ + 1) % presets_.size());
}

void FxController::previousPreset()
{
    if (!presets_.isEmpty()) selectPreset((current_ - 1 + presets_.size()) % presets_.size());
}

bool FxController::isModified() const
{
    return current_ >= 0 && current_ < presets_.size() && presets_[current_].modified;
}

void FxController::markModified()
{
    if (current_ < 0) return;
    dirty_ = true;
    if (!presets_[current_].modified) {
        presets_[current_].modified = true;
        emit modifiedChanged(true);
    }
}

void FxController::autoSave()
{
    if (current_ < 0) return;
    const QString name = presets_[current_].name;
    engine_.withDsp([&](DfxDsp &d) { return d.savePreset(w(name), w(dataDir() + "/autosave")); });
    dirty_ = false;
}

bool FxController::savePreset(QString &error)
{
    if (current_ < 0) return false;
    const Preset &p = presets_[current_];
    if (p.type == PresetType::Factory) {
        error = tr("Presets de fábrica não podem ser sobrescritos. Use \"Salvar como\".");
        return false;
    }
    return savePresetAs(p.name, error);
}

bool FxController::savePresetAs(const QString &raw_name, QString &error)
{
    if (!validPresetName(raw_name, error)) return false;
    const QString name = raw_name.trimmed();
    const QString old_name = current_ >= 0 ? presets_[current_].name : QString();

    int r = engine_.withDsp([&](DfxDsp &d) { return d.savePreset(w(name), w(userPresetDir())); });
    if (r != 0) { error = tr("Falha ao gravar o preset em %1.").arg(userPresetDir()); return false; }

    // as alterações agora fazem parte de um preset salvo
    QFile::remove(autoSavePath(old_name));
    QFile::remove(autoSavePath(name));
    dirty_ = false;

    scanPresets();
    for (int i = 0; i < presets_.size(); i++)
        if (presets_[i].name == name && presets_[i].type == PresetType::User) { current_ = i; break; }
    presets_[current_].modified = false;
    settings_.setValue("preset", name);
    emit presetSelected(current_);
    emit modifiedChanged(false);
    emit message(tr("Preset \"%1\" salvo.").arg(name));
    return true;
}

bool FxController::deletePreset(QString &error)
{
    if (current_ < 0) return false;
    const Preset p = presets_[current_];
    if (p.type != PresetType::User) { error = tr("Presets de fábrica não podem ser excluídos."); return false; }
    if (!QFile::remove(p.path)) { error = tr("Não foi possível excluir %1.").arg(p.path); return false; }
    QFile::remove(autoSavePath(p.name));
    dirty_ = false;
    int idx = std::max(0, current_ - 1);
    current_ = -1;
    scanPresets();
    selectPreset(std::min(idx, int(presets_.size()) - 1));
    emit message(tr("Preset \"%1\" excluído.").arg(p.name));
    return true;
}

void FxController::revertPreset()
{
    if (current_ < 0) return;
    QFile::remove(autoSavePath(presets_[current_].name));
    dirty_ = false;
    int idx = current_;
    current_ = -1; // força recarregar
    selectPreset(idx);
}

bool FxController::importPreset(const QString &file, QString &error)
{
    DfxPreset info = engine_.withDsp([&](DfxDsp &d) { return d.getPresetInfo(w(file)); });
    QString name = QString::fromStdWString(info.name).trimmed();
    if (name.isEmpty() || !validPresetName(name, error)) {
        if (error.isEmpty()) error = tr("O arquivo não é um preset válido do FxSound.");
        return false;
    }
    QString dest = userPresetDir() + "/" + name + ".fac";
    if (QFileInfo(file).absoluteFilePath() != QFileInfo(dest).absoluteFilePath()) {
        QFile::remove(dest);
        if (!QFile::copy(file, dest)) { error = tr("Não foi possível copiar para %1.").arg(dest); return false; }
    }
    scanPresets();
    for (int i = 0; i < presets_.size(); i++)
        if (presets_[i].name == name && presets_[i].type == PresetType::User) { selectPreset(i); break; }
    emit message(tr("Preset \"%1\" importado.").arg(name));
    return true;
}

bool FxController::exportPreset(const QString &file, QString &error)
{
    if (current_ < 0) return false;
    // Grava o estado atual (com alterações) num diretório temporário e copia
    // para o destino escolhido. (O exportPreset() do DSP altera o estado ao
    // vivo como efeito colateral, por isso não é usado aqui.)
    QTemporaryDir tmp;
    if (!tmp.isValid()) { error = tr("Não foi possível criar diretório temporário."); return false; }
    const QString name = presets_[current_].name;
    int r = engine_.withDsp([&](DfxDsp &d) { return d.savePreset(w(name), w(tmp.path())); });
    QString src = tmp.path() + "/" + name + ".fac";
    if (r != 0 || !QFile::exists(src)) { error = tr("Falha ao gerar o preset."); return false; }
    QFile::remove(file);
    if (!QFile::copy(src, file)) { error = tr("Não foi possível gravar %1.").arg(file); return false; }
    emit message(tr("Preset exportado para %1.").arg(file));
    return true;
}

// ----------------------------------------------------------------- efeitos

float FxController::effect(DfxDsp::Effect e)
{
    return engine_.withDsp([&](DfxDsp &d) { return d.getEffectValue(e) * 10.0f; });
}

void FxController::setEffect(DfxDsp::Effect e, float value)
{
    value = std::clamp(value, 0.0f, 10.0f);
    engine_.withDsp([&](DfxDsp &d) { d.setEffectValue(e, value); return 0; });
    markModified();
}

// ----------------------------------------------------------------------- EQ

int FxController::numBands()
{
    return engine_.withDsp([](DfxDsp &d) { return d.getNumEqBands(); });
}

void FxController::setNumBands(int n)
{
    if (n != 5 && n != 10 && n != 15 && n != 20 && n != 31) return;
    settings_.setValue("num_bands", n);
    if (dirty_) autoSave();
    engine_.withDsp([&](DfxDsp &d) { d.setNumBands(n); return 0; });
    // recarrega o preset para redistribuir as bandas a partir do arquivo
    int idx = current_;
    current_ = -1;
    selectPreset(idx);
    emit numBandsChanged(n);
}

float FxController::bandFrequency(int band)
{
    return engine_.withDsp([&](DfxDsp &d) { return d.getEqBandFrequency(band); });
}

void FxController::setBandFrequency(int band, float hz)
{
    engine_.withDsp([&](DfxDsp &d) { d.setEqBandFrequency(band, hz); return 0; });
    markModified();
}

void FxController::bandFrequencyRange(int band, float &lo, float &hi)
{
    engine_.withDsp([&](DfxDsp &d) { d.getEqBandFrequencyRange(band, &lo, &hi); return 0; });
}

float FxController::bandGain(int band)
{
    return engine_.withDsp([&](DfxDsp &d) { return d.getEqBandBoostCut(band); });
}

void FxController::setBandGain(int band, float db)
{
    db = std::clamp(db, -MaxEqGain, MaxEqGain);
    engine_.withDsp([&](DfxDsp &d) { d.setEqBandBoostCut(band, db); return 0; });
    markModified();
}

void FxController::resetEq()
{
    engine_.withDsp([](DfxDsp &d) {
        for (int b = 0; b < d.getNumEqBands(); b++) d.setEqBandBoostCut(b, 0.0f);
        return 0;
    });
    markModified();
}

// --------------------------------------------------------------- avançado

float FxController::masterGain() { return engine_.withDsp([](DfxDsp &d) { return d.getMasterGain(); }); }
float FxController::balance() { return engine_.withDsp([](DfxDsp &d) { return d.getBalance(); }); }
float FxController::volumeLeveling() { return engine_.withDsp([](DfxDsp &d) { return d.getVolumeLeveling(); }); }
float FxController::filterQ() { return engine_.withDsp([](DfxDsp &d) { return d.getFilterQ(); }); }

void FxController::setMasterGain(float v)
{
    v = std::clamp(v, -20.0f, 20.0f);
    settings_.setValue("master_gain", v);
    engine_.withDsp([&](DfxDsp &d) { d.setMasterGain(v); return 0; });
}

void FxController::setBalance(float v)
{
    v = std::clamp(v, -20.0f, 20.0f);
    settings_.setValue("balance", v);
    engine_.withDsp([&](DfxDsp &d) { d.setBalance(v); return 0; });
}

void FxController::setVolumeLeveling(float v)
{
    v = std::clamp(v, 0.0f, 4.0f);
    settings_.setValue("volume_leveling", v);
    engine_.withDsp([&](DfxDsp &d) { d.setVolumeLeveling(v); return 0; });
}

void FxController::setFilterQ(float v)
{
    v = std::clamp(v, 1.0f, 3.0f);
    settings_.setValue("filter_q", v);
    engine_.withDsp([&](DfxDsp &d) { d.setFilterQ(v); return 0; });
}

// ------------------------------------------------------------ dispositivos

QString FxController::outputDevice() const { return QString::fromStdString(engine_.outputDevice()); }
QString FxController::activeOutputDevice() const { return QString::fromStdString(engine_.activeOutputDevice()); }

void FxController::setOutputDevice(const QString &name)
{
    settings_.setValue("output_device", name);
    engine_.setOutputDevice(name.toStdString());
    emit devicesChanged();
}

bool FxController::manageDefaultSink() const { return settings_.value("manage_default_sink", true).toBool(); }

void FxController::setManageDefaultSink(bool on)
{
    settings_.setValue("manage_default_sink", on);
    engine_.setManageDefaultSink(on);
}

bool FxController::windowsVolumeCurve() const { return settings_.value("windows_volume_curve", true).toBool(); }

void FxController::setWindowsVolumeCurve(bool on)
{
    settings_.setValue("windows_volume_curve", on);
    engine_.setWindowsVolumeCurve(on);
}

void FxController::spectrum(float *values, int n)
{
    std::fill(values, values + n, 0.0f);
    if (!power_) return;
    engine_.withDsp([&](DfxDsp &d) { d.getSpectrumBandValues(values, n); return 0; });
    for (int i = 0; i < n; i++)
        if (!(values[i] >= 0.0f && values[i] <= 1.0f)) values[i] = 0.0f; // como o FxVisualizer original
}
