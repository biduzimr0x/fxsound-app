// Teste de fumaça do DSP original compilado no Linux.
// uso: fxdsp-selftest <diretório-com-presets .fac>
#include "DfxDsp.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

static std::wstring widen(const std::string &s) { return std::wstring(s.begin(), s.end()); }
static std::string narrow(const std::wstring &w)
{
    std::string s;
    for (wchar_t c : w) s += (c < 128) ? (char)c : '?';
    return s;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: %s <dir-presets>\n", argv[0]); return 2; }
    const int srate = 48000, nch = 2, frames = 512, blocks = 200;
    int failures = 0;

    std::vector<std::string> presets;
    if (DIR *d = opendir(argv[1])) {
        while (dirent *e = readdir(d))
            if (strstr(e->d_name, ".fac")) presets.push_back(std::string(argv[1]) + "/" + e->d_name);
        closedir(d);
    }
    if (presets.empty()) { fprintf(stderr, "nenhum preset em %s\n", argv[1]); return 1; }

    for (auto &path : presets) {
        DfxDsp dsp;
        DfxPreset info = dsp.getPresetInfo(widen(path));
        if (dsp.loadPreset(widen(path)) != 0) {
            printf("FALHA  loadPreset %s\n", path.c_str()); failures++; continue;
        }
        // mesma normalização que a GUI original faz após carregar um preset
        for (int e = 0; e < DfxDsp::NumEffects; e++)
            dsp.setEffectValue((DfxDsp::Effect)e, dsp.getEffectValue((DfxDsp::Effect)e) * 10);
        dsp.powerOn(true);
        dsp.setSignalFormat(32, nch, srate, 32);

        std::vector<float> buf(frames * nch);
        double in_rms = 0, out_rms = 0, diff = 0;
        bool finite = true;
        long n = 0;
        for (int b = 0; b < blocks; b++) {
            std::vector<float> in(frames * nch);
            for (int i = 0; i < frames; i++) {
                double t = (double)(b * frames + i) / srate;
                float v = 0.25f * (float)(sin(2 * M_PI * 100 * t) + 0.5 * sin(2 * M_PI * 3000 * t));
                in[i * 2] = in[i * 2 + 1] = v;
            }
            buf = in;
            dsp.processAudio((short *)buf.data(), (short *)buf.data(), frames, 0);
            if (b < blocks / 2) continue; // ignora o transiente inicial
            for (int i = 0; i < frames * nch; i++) {
                if (!std::isfinite(buf[i])) finite = false;
                in_rms += in[i] * in[i]; out_rms += buf[i] * buf[i];
                diff += (buf[i] - in[i]) * (buf[i] - in[i]); n++;
            }
        }
        in_rms = sqrt(in_rms / n); out_rms = sqrt(out_rms / n); diff = sqrt(diff / n);
        float spec[10] = {0};
        dsp.getSpectrumBandValues(spec, 10);

        bool ok = finite && out_rms > 1e-4 && diff > 1e-5;
        if (!ok) failures++;
        printf("%-6s %-28s bandas=%2d  rms in=%.3f out=%.3f  dif=%.3f  fid=%.1f amb=%.1f sur=%.1f boost=%.1f bass=%.1f  spec0=%.2f\n",
               ok ? "OK" : "FALHA", narrow(info.name).c_str(), dsp.getNumEqBands(), in_rms, out_rms, diff,
               dsp.getEffectValue(DfxDsp::Fidelity), dsp.getEffectValue(DfxDsp::Ambience),
               dsp.getEffectValue(DfxDsp::Surround), dsp.getEffectValue(DfxDsp::DynamicBoost),
               dsp.getEffectValue(DfxDsp::Bass), spec[0]);
    }

    // bypass deve ser transparente
    {
        DfxDsp dsp;
        dsp.loadPreset(widen(presets[0]));
        dsp.powerOn(false);
        dsp.setSignalFormat(32, nch, srate, 32);
        std::vector<float> in(frames * nch), buf;
        for (int i = 0; i < frames * nch; i++) in[i] = 0.1f * sinf(i * 0.01f);
        double maxd = 0;
        for (int b = 0; b < 20; b++) {
            buf = in;
            dsp.processAudio((short *)buf.data(), (short *)buf.data(), frames, 0);
            for (int i = 0; i < frames * nch; i++) maxd = fmax(maxd, fabs(buf[i] - in[i]));
        }
        bool ok = maxd < 1e-6;
        if (!ok) failures++;
        printf("%-6s bypass transparente (dif máx = %g)\n", ok ? "OK" : "FALHA", maxd);
    }

    // salvar -> recarregar deve preservar efeitos, EQ e nome (inclusive UTF-8)
    {
        char tmpl[] = "/tmp/fxdsp-selftest-XXXXXX";
        const char *dir = mkdtemp(tmpl);
        DfxDsp a;
        a.setNumBands(10);
        a.loadPreset(widen(presets[0]));
        float fx[DfxDsp::NumEffects] = {3, 7, 1, 9, 4};
        for (int e = 0; e < DfxDsp::NumEffects; e++) a.setEffectValue((DfxDsp::Effect)e, fx[e]);
        for (int b = 0; b < a.getNumEqBands(); b++) a.setEqBandBoostCut(b, (float)(b - 5));
        const std::wstring name = L"Teste Graves Ação";
        int rs = a.savePreset(name, widen(dir));
        std::wstring file = widen(dir) + L"/" + name + L".fac";

        DfxDsp b;
        b.setNumBands(10);
        DfxPreset info = b.getPresetInfo(file);
        int rl = b.loadPreset(file);
        bool ok = rs == 0 && rl == 0 && info.name == name;
        for (int e = 0; e < DfxDsp::NumEffects; e++) {
            float got = b.getEffectValue((DfxDsp::Effect)e) * 10;
            // o .fac guarda knobs quantizados (inteiros), então há erro de arredondamento
            if (fabs(got - fx[e]) > 0.05f) { ok = false; printf("   efeito %d: %.2f != %.2f\n", e, got, fx[e]); }
        }
        for (int i = 0; i < b.getNumEqBands(); i++) {
            float got = b.getEqBandBoostCut(i);
            if (fabs(got - (i - 5)) > 0.01f) { ok = false; printf("   banda %d: %.2f != %d\n", i, got, i - 5); }
        }
        if (!ok) failures++;
        printf("%-6s salvar/recarregar preset (save=%d load=%d nome=%s)\n", ok ? "OK" : "FALHA", rs, rl,
               info.name == name ? "igual" : "DIFERENTE");
        std::string cmd = std::string("rm -rf '") + dir + "'";
        if (system(cmd.c_str()) != 0) {}
    }

    printf("\n%d falha(s)\n", failures);
    return failures ? 1 : 0;
}
