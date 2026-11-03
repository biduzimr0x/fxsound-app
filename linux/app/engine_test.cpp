// Harness de teste do motor sem GUI e sem tocar no sink padrão do sistema.
// uso: fxsound-engine-test <preset.fac|-> <sink-de-saída> <segundos> [bypass]
#include "FxEngine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "uso: %s <preset.fac|-> <sink-de-saída> <segundos> [bypass]\n", argv[0]);
        return 2;
    }
    std::string preset = argv[1], out = argv[2];
    int secs = atoi(argv[3]);
    bool bypass = argc > 4 && std::string(argv[4]) == "bypass";

    const char *sink = getenv("FXSOUND_TEST_SINK");
    FxEngine engine(sink ? sink : FxEngine::DefaultSinkName);
    engine.setManageDefaultSink(false);
    engine.onError = [](const std::string &e) { fprintf(stderr, "ERRO: %s\n", e.c_str()); };

    if (preset != "-") {
        std::wstring w(preset.begin(), preset.end());
        int r = engine.withDsp([&](DfxDsp &d) {
            int rr = d.loadPreset(w);
            for (int e = 0; e < DfxDsp::NumEffects; e++)
                d.setEffectValue((DfxDsp::Effect)e, d.getEffectValue((DfxDsp::Effect)e) * 10);
            return rr;
        });
        printf("preset %s: %s\n", preset.c_str(), r == 0 ? "ok" : "FALHOU");
    }

    std::string err;
    if (!engine.start(err)) { fprintf(stderr, "start: %s\n", err.c_str()); return 1; }
    engine.setOutputDevice(out);
    engine.setPower(!bypass);

    printf("dispositivos:\n");
    for (auto &d : engine.devices()) printf("  %s  (%s)\n", d.name.c_str(), d.description.c_str());
    printf("saída ativa: %s\n", engine.activeOutputDevice().c_str());
    fflush(stdout);

    std::this_thread::sleep_for(std::chrono::seconds(secs));
    printf("taxa: %u Hz, processando: %s, underruns: %llu\n", engine.sampleRate(), engine.isProcessing() ? "sim" : "não", (unsigned long long)engine.underruns());
    engine.stop();
    return 0;
}
