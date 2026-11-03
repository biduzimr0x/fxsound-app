/*
FxSound for Linux
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#pragma once

#include "DfxDsp.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_stream;
struct pw_metadata;
struct pw_proxy;

/*
 * Motor de áudio: substitui o driver virtual + audiopassthru do Windows.
 *
 *   apps ──> [sink virtual "FxSound"] ──DfxDsp──> [dispositivo real]
 *            (stream de captura,                  (stream de reprodução)
 *             media.class=Audio/Sink)
 *
 * Com "power" ligado, o FxSound vira o sink padrão do sistema e o áudio
 * passa pelo DSP; desligado, o padrão anterior é restaurado (igual ao app
 * Windows, que troca o dispositivo padrão).
 */
class FxEngine
{
public:
    static constexpr const char *DefaultSinkName = "fxsound_sink";

    struct Device {
        uint32_t id = 0;
        std::string name;        // node.name (estável, usado p/ persistir)
        std::string description; // node.description (exibido)
    };

    // sink_name permite instâncias de teste isoladas ("fxsound_sink" no app)
    explicit FxEngine(std::string sink_name = DefaultSinkName);
    ~FxEngine();

    bool start(std::string &error);
    void stop();

    // Liga/desliga o processamento e a "tomada" do sink padrão.
    void setPower(bool on);
    bool power() const { return power_; }

    // Dispositivo de saída: nome do nó ou "" para seguir o padrão do sistema.
    void setOutputDevice(const std::string &node_name);
    std::string outputDevice() const;      // configuração (pode ser "")
    std::string activeOutputDevice() const; // dispositivo realmente em uso
    std::vector<Device> devices() const;

    // Se false, o FxSound não muda o sink padrão (o usuário roteia à mão).
    void setManageDefaultSink(bool manage);

    // Curva do volume do sink FxSound: true = mesma curva do Windows
    // (30% ≈ -18 dB); false = padrão do PipeWire (cúbica, 30% ≈ -31 dB).
    void setWindowsVolumeCurve(bool on);

    // Acesso serializado ao DSP (o thread de áudio usa o mesmo mutex).
    template <typename F> auto withDsp(F &&f) -> decltype(f(std::declval<DfxDsp &>()))
    {
        std::lock_guard<std::mutex> lk(dsp_mutex_);
        return f(dsp_);
    }

    // Callbacks chamados a partir do thread do PipeWire.
    std::function<void()> onDevicesChanged;
    std::function<void(const std::string &)> onError;

    // Estado do fluxo, para a UI.
    bool isProcessing() const { return frames_processed_.load() > 0; }
    uint32_t sampleRate() const { return rate_.load(); }

    // ---- uso interno (callbacks C do PipeWire) ----
    struct Impl;

private:
    friend struct Impl;
    void createStreams();
    void destroyStreams();
    void retargetIfNeeded();
    void applyDefaultSink();
    void restoreDefaultSink();
    std::string pickRealDevice() const;

    void processCapture();
    void processPlayback();

    std::unique_ptr<Impl> impl_;

    DfxDsp dsp_;
    std::mutex dsp_mutex_;

    pw_thread_loop *loop_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_registry *registry_ = nullptr;
    pw_metadata *metadata_ = nullptr;
    pw_proxy *metadata_proxy_ = nullptr;
    pw_stream *capture_ = nullptr;
    std::atomic<pw_stream *> playback_{nullptr};

    // estado (protegido pelo lock do thread loop)
    std::map<uint32_t, Device> sinks_;
    std::string configured_output_;  // escolha do usuário ("" = automático)
    std::string system_default_;     // último padrão do sistema que não é o FxSound
    std::string target_;             // alvo atual do stream de reprodução
    bool power_ = false;
    bool manage_default_ = true;
    bool we_set_default_ = false;
    bool started_ = false;

    // formato negociado (escrito no thread do PW, lido no thread RT)
    std::atomic<uint32_t> rate_{0};
    std::atomic<uint32_t> channels_{2};
    uint32_t dsp_rate_ = 0, dsp_channels_ = 0; // só no thread RT
    std::atomic<uint64_t> frames_processed_{0};

    // ring buffer SPSC entre o callback de captura (produtor, já processado
    // pelo DSP) e o de reprodução (consumidor). Ambos rodam em threads RT.
    static constexpr uint32_t RingFrames = 1u << 15;  // ~680 ms a 48 kHz
    std::vector<float> ring_;                         // RingFrames * 2 canais
    std::vector<float> scratch_;                      // buffer de trabalho do DSP
    std::atomic<uint64_t> ring_write_{0};             // em frames
    std::atomic<uint64_t> ring_read_{0};
    std::atomic<uint32_t> last_quantum_{1024};
    bool primed_ = false;                              // só no consumidor
    std::atomic<uint64_t> underruns_{0};

    // Volume do sink FxSound. O adapter do PipeWire o aplica na entrada
    // (antes do DSP); o motor desfaz esse ganho e o reaplica na saída, para
    // que o DSP (em especial o Dynamic Boost) receba o sinal em nível cheio,
    // como no Windows, onde o volume do sistema age depois do FxSound.
    std::atomic<float> in_gain_[2] = {1.0f, 1.0f};  // ganho aplicado pelo adapter
    std::atomic<float> out_gain_[2] = {1.0f, 1.0f}; // ganho a aplicar após o DSP
    bool windows_curve_ = true;
    float channel_vol_[2] = {1.0f, 1.0f}; // só no thread do PW
    float master_vol_ = 1.0f;
    bool muted_ = false;
    void updateInputGain();

    std::string sink_name_, output_name_, group_name_;
public:
    uint64_t underruns() const { return underruns_.load(); }
};
