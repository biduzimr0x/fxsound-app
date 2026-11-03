/*
FxSound for Linux
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#pragma once

#include "FxEngine.h"

#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QVector>

/*
 * Equivalente Linux do FxController do app Windows: presets, configurações
 * persistidas e a ponte entre a UI e o motor (FxEngine + DfxDsp).
 */
class FxController : public QObject
{
    Q_OBJECT
public:
    static constexpr int NumSpectrumBands = 10;
    static constexpr float MaxEqGain = 12.0f;

    enum class PresetType { Factory, User };
    struct Preset {
        QString name;
        QString path;
        PresetType type;
        bool modified = false;
    };

    explicit FxController(QObject *parent = nullptr);
    ~FxController() override;

    bool init(QString &error);
    void shutdown();

    // power
    bool power() const { return power_; }
    void setPower(bool on);

    // presets
    const QVector<Preset> &presets() const { return presets_; }
    int currentPreset() const { return current_; }
    void selectPreset(int index);
    void nextPreset();
    void previousPreset();
    bool isModified() const;
    bool savePreset(QString &error);                       // sobrescreve preset do usuário
    bool savePresetAs(const QString &name, QString &error); // cria/atualiza preset do usuário
    bool deletePreset(QString &error);                     // só presets do usuário
    void revertPreset();                                   // descarta alterações
    bool importPreset(const QString &file, QString &error);
    bool exportPreset(const QString &file, QString &error);
    QString userPresetDir() const;

    // efeitos (0..10)
    float effect(DfxDsp::Effect e);
    void setEffect(DfxDsp::Effect e, float value);

    // equalizador
    int numBands();
    void setNumBands(int n);
    float bandFrequency(int band);
    void setBandFrequency(int band, float hz);
    void bandFrequencyRange(int band, float &lo, float &hi);
    float bandGain(int band);
    void setBandGain(int band, float db);
    void resetEq();

    // ajustes avançados (mesmas faixas do app Windows)
    float masterGain();       void setMasterGain(float db);      // -20..20
    float balance();          void setBalance(float db);         // -20..20
    float volumeLeveling();   void setVolumeLeveling(float v);   // 0..4
    float filterQ();          void setFilterQ(float q);          // 1..3

    // dispositivos
    std::vector<FxEngine::Device> devices() const { return engine_.devices(); }
    QString outputDevice() const;          // "" = automático
    QString activeOutputDevice() const;
    void setOutputDevice(const QString &name);
    bool manageDefaultSink() const;
    void setManageDefaultSink(bool on);
    bool windowsVolumeCurve() const;
    void setWindowsVolumeCurve(bool on);

    // espectro (0..1 por banda)
    void spectrum(float *values, int n);
    uint32_t sampleRate() const { return engine_.sampleRate(); }

    QSettings &settings() { return settings_; }

signals:
    void powerChanged(bool on);
    void presetsChanged();
    void presetSelected(int index);   // valores do DSP mudaram
    void modifiedChanged(bool modified);
    void devicesChanged();
    void numBandsChanged(int n);
    void errorOccurred(const QString &message);
    void message(const QString &message);

private:
    void scanPresets();
    void markModified();
    void autoSave();
    QString autoSavePath(const QString &preset_name) const;
    void syncDspAfterLoad();
    void applyAdvancedSettings();
    void moveVolumeToFx(int attempt = 0);
    void moveVolumeToDevice();
    static std::wstring w(const QString &s) { return s.toStdWString(); }

    FxEngine engine_;
    QSettings settings_;
    QVector<Preset> presets_;
    int current_ = -1;
    bool power_ = false;
    bool dirty_ = false;      // há alteração ainda não auto-salva
    QTimer autosave_timer_;
    QString volume_device_;   // dispositivo cujo volume foi transferido p/ o FxSound
    bool shut_down_ = false;
};
