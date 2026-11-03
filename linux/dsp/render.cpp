// Processa um arquivo float32 estéreo bruto (48 kHz) com um preset, offline.
// uso: fxdsp-render <preset.fac> <entrada.f32> <saída.f32>
#include "DfxDsp.h"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "uso: %s <preset.fac> <in.f32> <out.f32>\n", argv[0]); return 2; }
    DfxDsp dsp;
    std::string p = argv[1];
    if (dsp.loadPreset(std::wstring(p.begin(), p.end())) != 0) { fprintf(stderr, "preset falhou\n"); return 1; }
    for (int e = 0; e < DfxDsp::NumEffects; e++)
        dsp.setEffectValue((DfxDsp::Effect)e, dsp.getEffectValue((DfxDsp::Effect)e) * 10);
    dsp.powerOn(true);
    dsp.setSignalFormat(32, 2, 48000, 32);
    FILE *in = fopen(argv[2], "rb"), *out = fopen(argv[3], "wb");
    if (!in || !out) { perror("fopen"); return 1; }
    std::vector<float> buf(1024 * 2);
    size_t n;
    while ((n = fread(buf.data(), sizeof(float) * 2, 1024, in)) > 0) {
        dsp.processAudio((short *)buf.data(), (short *)buf.data(), (int)n, 0);
        fwrite(buf.data(), sizeof(float) * 2, n, out);
    }
    fclose(in); fclose(out);
    return 0;
}
