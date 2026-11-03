/*
FxSound for Linux
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#include "FxEngine.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/utils/json.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr uint32_t kChannels = 2;
constexpr uint32_t kStride = sizeof(float) * kChannels;

// Extrai "name" de um valor JSON de metadata como { "name": "alsa_output..." }
std::string json_name(const char *value)
{
    if (!value) return {};
    struct spa_json it[2];
    char key[64];
    spa_json_init(&it[0], value, strlen(value));
    if (spa_json_enter_object(&it[0], &it[1]) <= 0) return {};
    while (spa_json_get_string(&it[1], key, sizeof(key)) > 0) {
        if (strcmp(key, "name") == 0) {
            char name[512];
            if (spa_json_get_string(&it[1], name, sizeof(name)) > 0) return name;
            return {};
        }
        const char *v;
        if (spa_json_next(&it[1], &v) <= 0) break;
    }
    return {};
}

// RAII para o lock do thread loop (chamadas vindas da UI).
struct LoopLock {
    pw_thread_loop *l;
    explicit LoopLock(pw_thread_loop *loop) : l(loop) { if (l) pw_thread_loop_lock(l); }
    ~LoopLock() { if (l) pw_thread_loop_unlock(l); }
};

} // namespace

struct FxEngine::Impl {
    FxEngine *self;
    spa_hook core_listener{};
    spa_hook registry_listener{};
    spa_hook metadata_listener{};
    spa_hook capture_listener{};
    spa_hook playback_listener{};
    std::map<uint32_t, int> sink_priority;
    std::vector<pw_stream *> graveyard; // streams de reprodução antigos
    int pending_sync = -1;
    bool sync_done = false;

    // ---------------------------------------------------------------- core
    static void on_core_done(void *data, uint32_t id, int seq)
    {
        auto *impl = static_cast<Impl *>(data);
        if (id == PW_ID_CORE && seq == impl->pending_sync) {
            impl->sync_done = true;
            pw_thread_loop_signal(impl->self->loop_, false);
        }
    }

    static void on_core_error(void *data, uint32_t id, int, int res, const char *message)
    {
        auto *impl = static_cast<Impl *>(data);
        if (id == PW_ID_CORE && res == -EPIPE && impl->self->onError)
            impl->self->onError(std::string("Conexão com o PipeWire perdida: ") + (message ? message : ""));
    }

    // ------------------------------------------------------------ registry
    static void on_global(void *data, uint32_t id, uint32_t, const char *type, uint32_t,
                          const spa_dict *props)
    {
        auto *impl = static_cast<Impl *>(data);
        FxEngine *e = impl->self;
        if (!props) return;

        if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            const char *cls = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
            const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
            if (!cls || !name || strcmp(cls, "Audio/Sink") != 0) return;
            if (name == e->sink_name_) return;
            const char *desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
            if (!desc) desc = spa_dict_lookup(props, PW_KEY_NODE_NICK);
            const char *prio = spa_dict_lookup(props, PW_KEY_PRIORITY_SESSION);
            e->sinks_[id] = Device{id, name, desc ? desc : name};
            impl->sink_priority[id] = prio ? atoi(prio) : 0;
            e->retargetIfNeeded();
            if (e->onDevicesChanged) e->onDevicesChanged();
        } else if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
            const char *mname = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
            if (!mname || strcmp(mname, "default") != 0 || e->metadata_) return;
            e->metadata_ = static_cast<pw_metadata *>(
                pw_registry_bind(e->registry_, id, type, PW_VERSION_METADATA, 0));
            e->metadata_proxy_ = reinterpret_cast<pw_proxy *>(e->metadata_);
            static const pw_metadata_events mev = {
                .version = PW_VERSION_METADATA_EVENTS,
                .property = on_metadata_property,
            };
            pw_metadata_add_listener(e->metadata_, &impl->metadata_listener, &mev, impl);
        }
    }

    static void on_global_remove(void *data, uint32_t id)
    {
        auto *impl = static_cast<Impl *>(data);
        FxEngine *e = impl->self;
        if (e->sinks_.erase(id)) {
            impl->sink_priority.erase(id);
            e->retargetIfNeeded();
            if (e->onDevicesChanged) e->onDevicesChanged();
        }
    }

    // ------------------------------------------------------------ metadata
    static int on_metadata_property(void *data, uint32_t subject, const char *key, const char *,
                                    const char *value)
    {
        auto *impl = static_cast<Impl *>(data);
        FxEngine *e = impl->self;
        if (subject != PW_ID_CORE || !key) return 0;

        if (strcmp(key, "default.audio.sink") == 0) {
            std::string name = json_name(value);
            if (!name.empty() && name != e->sink_name_) {
                // O usuário (ou o WirePlumber) escolheu outro dispositivo:
                // ele passa a ser a saída real e o FxSound retoma o padrão.
                e->system_default_ = name;
                e->retargetIfNeeded();
                if (e->power_ && e->manage_default_) e->applyDefaultSink();
                if (e->onDevicesChanged) e->onDevicesChanged();
            }
        }
        return 0;
    }

    // ------------------------------------------------------------- streams
    static void on_capture_process(void *data) { static_cast<Impl *>(data)->self->processCapture(); }
    static void on_playback_process(void *data) { static_cast<Impl *>(data)->self->processPlayback(); }

    static void on_param_changed(void *data, uint32_t id, const spa_pod *param)
    {
        auto *impl = static_cast<Impl *>(data);
        if (!param || id != SPA_PARAM_Format) return;
        spa_audio_info_raw info{};
        if (spa_format_audio_raw_parse(param, &info) < 0) return;
        impl->self->rate_ = info.rate;
        impl->self->channels_ = info.channels;
    }

    // Volume/mute do sink FxSound, definidos pelo usuário (teclas, waybar...)
    static void on_control_info(void *data, uint32_t id, const pw_stream_control *control)
    {
        auto *impl = static_cast<Impl *>(data);
        FxEngine *e = impl->self;
        if (!control || control->n_values == 0) return;
        switch (id) {
        case SPA_PROP_channelVolumes:
            for (uint32_t c = 0; c < 2; c++)
                e->channel_vol_[c] = control->values[std::min<uint32_t>(c, control->n_values - 1)];
            break;
        case SPA_PROP_volume: e->master_vol_ = control->values[0]; break;
        case SPA_PROP_mute: e->muted_ = control->values[0] > 0.5f; break;
        default: return;
        }
        e->updateInputGain();
    }

    static void on_state_changed(void *data, pw_stream_state, pw_stream_state state, const char *error)
    {
        auto *impl = static_cast<Impl *>(data);
        if (state == PW_STREAM_STATE_ERROR && impl->self->onError)
            impl->self->onError(std::string("Erro no stream de áudio: ") + (error ? error : "?"));
    }
};

static const pw_core_events kCoreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .done = FxEngine::Impl::on_core_done,
    .error = FxEngine::Impl::on_core_error,
};

static const pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = FxEngine::Impl::on_global,
    .global_remove = FxEngine::Impl::on_global_remove,
};

static const pw_stream_events kCaptureEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = FxEngine::Impl::on_state_changed,
    .control_info = FxEngine::Impl::on_control_info,
    .param_changed = FxEngine::Impl::on_param_changed,
    .process = FxEngine::Impl::on_capture_process,
};

static const pw_stream_events kPlaybackEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = FxEngine::Impl::on_state_changed,
    .process = FxEngine::Impl::on_playback_process,
};

// =====================================================================

FxEngine::FxEngine(std::string sink_name)
    : impl_(std::make_unique<Impl>()), sink_name_(std::move(sink_name)),
      output_name_(sink_name_ == DefaultSinkName ? std::string("fxsound_output") : sink_name_ + "_output"),
      group_name_(sink_name_ + "-group")
{
    impl_->self = this;
    ring_.assign(size_t(RingFrames) * kChannels, 0.0f);
    scratch_.assign(size_t(RingFrames) * kChannels, 0.0f);
}

FxEngine::~FxEngine() { stop(); }

bool FxEngine::start(std::string &error)
{
    if (started_) return true;
    pw_init(nullptr, nullptr);

    loop_ = pw_thread_loop_new("fxsound", nullptr);
    if (!loop_) { error = "não foi possível criar o loop do PipeWire"; return false; }
    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (!context_) { error = "não foi possível criar o contexto do PipeWire"; return false; }

    pw_thread_loop_lock(loop_);
    if (pw_thread_loop_start(loop_) < 0) {
        pw_thread_loop_unlock(loop_);
        error = "não foi possível iniciar o thread do PipeWire";
        return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (!core_) {
        pw_thread_loop_unlock(loop_);
        error = "não foi possível conectar ao PipeWire (o serviço está rodando?)";
        return false;
    }
    pw_core_add_listener(core_, &impl_->core_listener, &kCoreEvents, impl_.get());
    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(registry_, &impl_->registry_listener, &kRegistryEvents, impl_.get());

    // 1º roundtrip: dispositivos e objeto de metadata; 2º: propriedades do
    // metadata (sink padrão). Só então criamos os streams, já com o alvo certo.
    for (int round = 0; round < 2; round++) {
        impl_->sync_done = false;
        impl_->pending_sync = pw_core_sync(core_, PW_ID_CORE, 0);
        while (!impl_->sync_done)
            if (pw_thread_loop_timed_wait(loop_, 3) != 0) break;
    }
    createStreams();
    started_ = true;
    pw_thread_loop_unlock(loop_);
    return true;
}

void FxEngine::stop()
{
    if (!started_) return;
    pw_thread_loop_lock(loop_);
    if (we_set_default_) {
        restoreDefaultSink();
        // garante que a mensagem chegou ao servidor antes de desconectar
        impl_->sync_done = false;
        impl_->pending_sync = pw_core_sync(core_, PW_ID_CORE, 0);
        while (!impl_->sync_done)
            if (pw_thread_loop_timed_wait(loop_, 2) != 0) break;
    }
    destroyStreams();
    if (metadata_proxy_) { spa_hook_remove(&impl_->metadata_listener); pw_proxy_destroy(metadata_proxy_); }
    metadata_ = nullptr; metadata_proxy_ = nullptr;
    if (registry_) { spa_hook_remove(&impl_->registry_listener); pw_proxy_destroy((pw_proxy *)registry_); }
    registry_ = nullptr;
    spa_hook_remove(&impl_->core_listener);
    pw_core_disconnect(core_);
    core_ = nullptr;
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_stop(loop_);
    pw_context_destroy(context_);
    pw_thread_loop_destroy(loop_);
    context_ = nullptr; loop_ = nullptr;
    started_ = false;
}

// ------------------------------------------------------------------ streams

static const spa_pod *build_format(spa_pod_builder *b)
{
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    // rate = 0: usa a taxa do grafo; o adapter do PipeWire converte se preciso
    return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

static pw_stream *make_playback(pw_core *core, const std::string &target, const std::string &node_name,
                                const std::string &group, spa_hook *hook, void *data)
{
    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_NODE_NAME, node_name.c_str(),
        PW_KEY_NODE_DESCRIPTION, "FxSound (saída processada)",
        PW_KEY_APP_NAME, "FxSound",
        PW_KEY_APP_ICON_NAME, "fxsound",
        PW_KEY_NODE_GROUP, group.c_str(),
        // mesmo link-group do sink: o WirePlumber nunca liga um no outro,
        // evitando realimentação mesmo se o FxSound for o padrão
        PW_KEY_NODE_LINK_GROUP, group.c_str(),
        "audio.position", "[ FL FR ]",
        nullptr);
    if (!target.empty()) pw_properties_set(props, PW_KEY_TARGET_OBJECT, target.c_str());

    pw_stream *s = pw_stream_new(core, "fxsound-output", props);
    if (!s) return nullptr;
    pw_stream_add_listener(s, hook, &kPlaybackEvents, data);

    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    const spa_pod *params[1] = {build_format(&b)};
    pw_stream_connect(s, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                      (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                        PW_STREAM_FLAG_RT_PROCESS),
                      params, 1);
    return s;
}

void FxEngine::createStreams()
{
    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        PW_KEY_NODE_NAME, sink_name_.c_str(),
        PW_KEY_NODE_DESCRIPTION, "FxSound",
        PW_KEY_APP_NAME, "FxSound",
        PW_KEY_APP_ICON_NAME, "fxsound",
        PW_KEY_DEVICE_ICON_NAME, "fxsound",
        PW_KEY_NODE_GROUP, group_name_.c_str(),
        PW_KEY_NODE_LINK_GROUP, group_name_.c_str(),
        PW_KEY_NODE_VIRTUAL, "true",
        "audio.position", "[ FL FR ]",
        nullptr);
    capture_ = pw_stream_new(core_, "fxsound-sink", props);
    pw_stream_add_listener(capture_, &impl_->capture_listener, &kCaptureEvents, impl_.get());

    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    const spa_pod *params[1] = {build_format(&b)};
    pw_stream_connect(capture_, PW_DIRECTION_INPUT, PW_ID_ANY,
                      (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                        PW_STREAM_FLAG_RT_PROCESS),
                      params, 1);

    target_ = pickRealDevice();
    playback_ = make_playback(core_, target_, output_name_, group_name_, &impl_->playback_listener, impl_.get());
}

void FxEngine::destroyStreams()
{
    if (capture_) {
        spa_hook_remove(&impl_->capture_listener);
        pw_stream_destroy(capture_);
        capture_ = nullptr;
    }
    if (pw_stream *pb = playback_.exchange(nullptr)) {
        spa_hook_remove(&impl_->playback_listener);
        pw_stream_destroy(pb);
    }
    for (pw_stream *s : impl_->graveyard) pw_stream_destroy(s);
    impl_->graveyard.clear();
}

std::string FxEngine::pickRealDevice() const
{
    auto exists = [&](const std::string &n) {
        return !n.empty() && std::any_of(sinks_.begin(), sinks_.end(),
                                         [&](auto &kv) { return kv.second.name == n; });
    };
    if (exists(configured_output_)) return configured_output_;
    if (exists(system_default_)) return system_default_;
    // sem padrão conhecido: o dispositivo de maior prioridade
    std::string best;
    int best_prio = -1;
    for (auto &[id, dev] : sinks_) {
        auto it = impl_->sink_priority.find(id);
        int p = it != impl_->sink_priority.end() ? it->second : 0;
        if (p > best_prio) { best_prio = p; best = dev.name; }
    }
    return best;
}

void FxEngine::retargetIfNeeded()
{
    if (!core_ || !capture_) return;
    std::string want = pickRealDevice();
    if (want == target_ && playback_.load()) return;
    target_ = want;

    // O stream antigo é desconectado mas só destruído depois: o callback de
    // captura (thread RT) pode estar segurando o ponteiro neste instante.
    if (pw_stream *old = playback_.exchange(nullptr)) {
        spa_hook_remove(&impl_->playback_listener);
        pw_stream_disconnect(old);
        for (pw_stream *s : impl_->graveyard) pw_stream_destroy(s);
        impl_->graveyard.clear();
        impl_->graveyard.push_back(old);
    }
    impl_->playback_listener = {};
    playback_ = make_playback(core_, target_, output_name_, group_name_, &impl_->playback_listener, impl_.get());
}

// --------------------------------------------------------------- sink padrão

void FxEngine::applyDefaultSink()
{
    if (!metadata_) return;
    char json[256];
    snprintf(json, sizeof(json), "{ \"name\": \"%s\" }", sink_name_.c_str());
    pw_metadata_set_property(metadata_, PW_ID_CORE, "default.configured.audio.sink", "Spa:String:JSON", json);
    we_set_default_ = true;
}

void FxEngine::restoreDefaultSink()
{
    if (!metadata_) return;
    if (!target_.empty()) {
        std::string json = "{ \"name\": \"" + target_ + "\" }";
        pw_metadata_set_property(metadata_, PW_ID_CORE, "default.configured.audio.sink", "Spa:String:JSON",
                                 json.c_str());
    } else {
        pw_metadata_set_property(metadata_, PW_ID_CORE, "default.configured.audio.sink", nullptr, nullptr);
    }
    we_set_default_ = false;
}

// Curva de volume do Windows (controle em software, faixa -96..0 dB), em dB,
// para 0%, 2%, 4% ... 100%. Medida no Windows 10 com IAudioEndpointVolume:
// https://gist.github.com/mr-salty/b76cb505acebe58ad57e832f018053b5
static const float kWindowsCurveDb[51] = {
    -96.00f, -58.17f, -48.28f, -42.34f, -38.08f, -34.75f, -32.03f, -29.72f, -27.71f, -25.94f,
    -24.35f, -22.92f, -21.61f, -20.40f, -19.28f, -18.24f, -17.26f, -16.34f, -15.48f, -14.66f,
    -13.89f, -13.15f, -12.44f, -11.77f, -11.13f, -10.51f,  -9.91f,  -9.34f,  -8.79f,  -8.26f,
     -7.75f,  -7.25f,  -6.77f,  -6.30f,  -5.85f,  -5.41f,  -4.98f,  -4.57f,  -4.16f,  -3.77f,
     -3.38f,  -3.01f,  -2.64f,  -2.29f,  -1.94f,  -1.60f,  -1.27f,  -0.94f,  -0.62f,  -0.31f,
      0.00f};

// percent: a "porcentagem" que o desktop mostra (raiz cúbica do ganho linear).
static float windows_gain(float percent)
{
    if (percent <= 0.0f) return 0.0f;
    if (percent >= 1.0f) return percent * percent * percent; // acima de 100%: igual ao PipeWire
    float x = percent * 50.0f;
    int i = std::min(49, (int)x);
    float t = x - (float)i;
    float db = kWindowsCurveDb[i] * (1.0f - t) + kWindowsCurveDb[i + 1] * t;
    if (i == 0) db = std::max(db, 20.0f * log10f(percent) - 24.2f); // perto de 0%: cai até silêncio
    return powf(10.0f, db / 20.0f);
}

void FxEngine::updateInputGain()
{
    for (int c = 0; c < 2; c++) {
        float g = muted_ ? 0.0f : channel_vol_[c] * master_vol_;
        in_gain_[c].store(g, std::memory_order_relaxed);
        float out = g;
        if (windows_curve_ && g > 0.0f) out = windows_gain(cbrtf(g));
        out_gain_[c].store(out, std::memory_order_relaxed);
    }
}

void FxEngine::setWindowsVolumeCurve(bool on)
{
    LoopLock lk(loop_);
    windows_curve_ = on;
    updateInputGain();
}

// ------------------------------------------------------------ API para a UI

void FxEngine::setPower(bool on)
{
    {
        std::lock_guard<std::mutex> lk(dsp_mutex_);
        dsp_.powerOn(on);
    }
    LoopLock lk(loop_);
    power_ = on;
    if (!started_ || !manage_default_) return;
    if (on) applyDefaultSink();
    else if (we_set_default_) restoreDefaultSink();
}

void FxEngine::setManageDefaultSink(bool manage)
{
    LoopLock lk(loop_);
    if (manage == manage_default_) return;
    manage_default_ = manage;
    if (!started_) return;
    if (manage && power_) applyDefaultSink();
    else if (!manage && we_set_default_) restoreDefaultSink();
}

void FxEngine::setOutputDevice(const std::string &node_name)
{
    LoopLock lk(loop_);
    configured_output_ = node_name;
    retargetIfNeeded();
}

std::string FxEngine::outputDevice() const
{
    LoopLock lk(loop_);
    return configured_output_;
}

std::string FxEngine::activeOutputDevice() const
{
    LoopLock lk(loop_);
    return target_;
}

std::vector<FxEngine::Device> FxEngine::devices() const
{
    LoopLock lk(loop_);
    std::vector<Device> out;
    for (auto &kv : sinks_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.description < b.description; });
    return out;
}

// ------------------------------------------------------ thread de tempo real
//
// Captura e reprodução estão no mesmo node.group, então são dirigidos pelo
// mesmo driver (o dispositivo real), mas a ordem entre os dois dentro de um
// ciclo não é garantida. Um ring buffer SPSC desacopla os dois: a captura
// processa no DSP e escreve; a reprodução lê com um pré-buffer de um quantum.

void FxEngine::processCapture()
{
    pw_buffer *in = pw_stream_dequeue_buffer(capture_);
    if (!in) return;
    spa_data &d = in->buffer->datas[0];
    uint32_t off = std::min(d.chunk->offset, d.maxsize);
    uint32_t size = std::min(d.chunk->size, d.maxsize - off);
    const float *src = d.data ? static_cast<const float *>(SPA_PTROFF(d.data, off, void)) : nullptr;
    uint32_t frames = std::min<uint32_t>(size / kStride, RingFrames / 2);

    if (src && frames > 0) {
        float *work = scratch_.data();
        // desfaz o volume aplicado pelo adapter na entrada (ver in_gain_)
        const float g[2] = {in_gain_[0].load(std::memory_order_relaxed), in_gain_[1].load(std::memory_order_relaxed)};
        const float inv[2] = {g[0] > 1e-7f ? 1.0f / g[0] : 0.0f, g[1] > 1e-7f ? 1.0f / g[1] : 0.0f};
        for (uint32_t i = 0; i < frames; i++) {
            work[2 * i] = src[2 * i] * inv[0];
            work[2 * i + 1] = src[2 * i + 1] * inv[1];
        }
        {
            uint32_t rate = rate_.load();
            std::lock_guard<std::mutex> lk(dsp_mutex_);
            if (rate && rate != dsp_rate_) {
                dsp_.setSignalFormat(32, kChannels, (int)rate, 32);
                dsp_rate_ = rate;
            }
            if (dsp_rate_)
                dsp_.processAudio(reinterpret_cast<short *>(work), reinterpret_cast<short *>(work), (int)frames, 0);
        }
        // ... e reaplica o volume depois do DSP (na curva escolhida)
        const float og[2] = {out_gain_[0].load(std::memory_order_relaxed), out_gain_[1].load(std::memory_order_relaxed)};
        for (uint32_t i = 0; i < frames; i++) {
            work[2 * i] *= og[0];
            work[2 * i + 1] *= og[1];
        }
        last_quantum_.store(frames, std::memory_order_relaxed);

        uint64_t w = ring_write_.load(std::memory_order_relaxed);
        uint64_t r = ring_read_.load(std::memory_order_acquire);
        uint64_t free_frames = RingFrames - (w - r);
        uint32_t n = (uint32_t)std::min<uint64_t>(frames, free_frames); // cheio: descarta o excesso
        for (uint32_t i = 0; i < n;) {
            uint32_t pos = (uint32_t)((w + i) % RingFrames);
            uint32_t chunk = std::min(n - i, RingFrames - pos);
            memcpy(&ring_[size_t(pos) * kChannels], work + size_t(i) * kChannels, chunk * kStride);
            i += chunk;
        }
        ring_write_.store(w + n, std::memory_order_release);
        frames_processed_.fetch_add(frames, std::memory_order_relaxed);
    }
    pw_stream_queue_buffer(capture_, in);
}

void FxEngine::processPlayback()
{
    pw_stream *pb = playback_.load();
    if (!pb) return;
    pw_buffer *out = pw_stream_dequeue_buffer(pb);
    if (!out) return;

    spa_data &od = out->buffer->datas[0];
    float *dst = static_cast<float *>(od.data);
    uint32_t capacity = dst ? od.maxsize / kStride : 0;
    uint32_t n = capacity;
    if (out->requested) n = std::min<uint32_t>(n, (uint32_t)out->requested);

    uint64_t r = ring_read_.load(std::memory_order_relaxed);
    uint64_t w = ring_write_.load(std::memory_order_acquire);
    uint64_t avail = w - r;
    uint32_t quantum = std::max<uint32_t>(last_quantum_.load(std::memory_order_relaxed), n);

    // Latência alvo: entre 1 e 2 quanta. Se acumular demais (ex.: deriva ou
    // pausa do consumidor), descarta o excesso mais antigo.
    if (avail > uint64_t(quantum) * 3) {
        r = w - uint64_t(quantum) * 2;
        avail = w - r;
    }
    if (!primed_ && avail >= n + quantum / 2) primed_ = true;

    uint32_t got = 0;
    if (primed_ && dst) {
        got = (uint32_t)std::min<uint64_t>(avail, n);
        for (uint32_t i = 0; i < got;) {
            uint32_t pos = (uint32_t)((r + i) % RingFrames);
            uint32_t chunk = std::min(got - i, RingFrames - pos);
            memcpy(dst + size_t(i) * kChannels, &ring_[size_t(pos) * kChannels], chunk * kStride);
            i += chunk;
        }
        r += got;
        if (got < n) { underruns_.fetch_add(1, std::memory_order_relaxed); primed_ = false; }
    }
    ring_read_.store(r, std::memory_order_release);
    if (dst && got < n) memset(dst + size_t(got) * kChannels, 0, (n - got) * kStride);

    od.chunk->offset = 0;
    od.chunk->stride = kStride;
    od.chunk->size = n * kStride;
    pw_stream_queue_buffer(pb, out);
}
