# FxSound para Linux (PipeWire)

Port não-oficial do FxSound para Linux. O **motor DSP é o original** (`dsp/`),
compilado sem mudanças nos algoritmos. O driver virtual do Windows e o
`audiopassthru` foram substituídos por um motor PipeWire nativo, e a GUI JUCE
por uma GUI Qt6.

```
apps ──> [sink "FxSound"] ──> DfxDsp (original) ──> [dispositivo real]
```

Quando ligado, o FxSound vira a saída padrão do sistema. Quando desligado ou
fechado, o padrão anterior volta, como no app do Windows. Se você trocar o
dispositivo padrão em outro lugar (pavucontrol, waybar etc.) com o FxSound
ligado, ele passa a usar esse dispositivo como saída e retoma o posto de padrão.

## Instalar (Arch Linux)

```sh
cd linux/pkg
makepkg -si
```

Dependências: `pipewire`, `qt6-base` (e `wireplumber` para a troca automática
do padrão). Para compilar: `meson`, `ninja`, `python`.

## Desenvolvimento

```sh
meson setup build
ninja -C build
meson test -C build            # carrega/processa os 12 presets, bypass, salvar/recarregar
./build/linux/app/fxsound      # usa os presets de bin/x64/Factsoft se não estiver instalado
```

Build com sanitizers: `meson setup build-asan -Dbuildtype=debug -Db_sanitize=address,undefined`.

## Uso

- Bandeja: clique para mostrar ou ocultar a janela; clique do meio liga ou desliga.
- Atalhos na janela: `Ctrl+P` liga/desliga, `Ctrl+←/→` troca de preset, `Ctrl+S` salva.
- Linha de comando (controla a instância em execução):
  `fxsound --toggle | --on | --off | --next | --prev | --preset NOME | --show | --quit`

### Hyprland

Não há atalhos globais no Wayland, então use os comandos acima nos binds:

```ini
exec-once = fxsound --minimized
bind = SUPER, F9,  exec, fxsound --toggle
bind = SUPER, F10, exec, fxsound --next
```

Para o ícone de bandeja aparecer, o waybar precisa do módulo `tray`.

## Arquivos

| O quê | Onde |
|---|---|
| Configuração | `~/.config/fxsound/fxsound.conf` |
| Presets do usuário | `~/.local/share/fxsound/presets/*.fac` (mesmo formato do Windows) |
| Alterações não salvas | `~/.local/share/fxsound/autosave/` |
| Presets de fábrica | `/usr/share/fxsound/presets/` |

Os presets `.fac` são compatíveis nos dois sentidos com o FxSound do Windows,
incluindo os de `bin/BonusPresets` (use *Importar preset*).

## Como o port funciona

- `linux/compat/fx_compat.h` é incluído à força em cada unidade do DSP. Ele
  fornece os tipos Win32 e o CRT wide da Microsoft, e reproduz o modelo de dados
  **LLP64** (`long` = 32 bits). O DSP usa arrays de `long` como "memória DSP" de
  32 bits, compartilhada com `float`; no LP64 do Linux isso corrompe memória.
- As funções wide de stream (`fgetws`, `fwprintf`) operam sobre bytes UTF-8,
  porque o parser de presets mistura `fgets` e `fgetws` no mesmo `FILE*`. Isso
  funciona no MSVC, mas no glibc o stream tem uma orientação só.
- `linux/gen_include_shims.py` cria symlinks para os `#include` escritos com
  caixa diferente ou com `\`, para que os fontes em `dsp/` não precisem mudar.
- `linux/app/FxEngine.cpp`: o stream de captura (`media.class=Audio/Sink`)
  processa no DSP e escreve num ring buffer SPSC. O stream de reprodução lê do
  ring com pré-buffer de um quantum. Os dois ficam no mesmo `node.group` e
  `node.link-group`, então o WirePlumber nunca liga a saída na entrada.
- O volume do sink FxSound (teclas de volume, waybar) é aplicado pelo PipeWire
  na entrada. O motor desfaz esse ganho antes do DSP e o reaplica na saída, então
  o DSP sempre recebe o sinal em nível cheio, como no Windows. Sem isso, o Dynamic
  Boost quase não age em volumes baixos.

## Limitações conhecidas

- O processamento é estéreo. Saídas 5.1 recebem o upmix do PipeWire.
- Há uma alteração em `dsp/`: `DfxDspPrivate.cpp` agora inicializa
  `preset_list_handle_`, que antes era lido sem inicialização no destrutor (bug
  também presente no Windows).
