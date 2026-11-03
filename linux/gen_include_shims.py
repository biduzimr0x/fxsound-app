#!/usr/bin/env python3
"""
Gera "shims" de include para compilar o código Windows original no Linux.

O código do FxSound foi escrito para Windows, onde o sistema de arquivos não
diferencia maiúsculas/minúsculas e '\\' é separador de caminho. Este script
varre todos os #include "..." dos fontes e, para cada um que não resolve no
Linux, cria um symlink com o nome EXATO usado no código apontando para o
arquivo real. Assim os fontes originais em dsp/ ficam intocados.

uso: gen_include_shims.py <raiz-do-repo> <dir-de-saída> [stamp]
"""
import os, re, sys

root = os.path.abspath(sys.argv[1])
out = os.path.abspath(sys.argv[2])
os.makedirs(out, exist_ok=True)

SCAN = ["dsp", "audiopassthru/include"]
inc_re = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)

# índice de todos os headers por nome (minúsculo) -> caminhos
index = {}
src_files = []
for top in SCAN:
    for dp, _, fns in os.walk(os.path.join(root, top)):
        for fn in fns:
            p = os.path.join(dp, fn)
            low = fn.lower()
            if low.endswith((".h", ".hpp")):
                index.setdefault(low, []).append(p)
            if low.endswith((".h", ".hpp", ".c", ".cpp")):
                src_files.append(p)

# preferência quando há nomes duplicados (File.h existe em dsp e audiopassthru)
def pick(cands):
    cands = sorted(cands, key=lambda p: (0 if "/dsp/" in p else 1, len(p)))
    return cands[0]

made = 0
for sf in src_files:
    try:
        text = open(sf, encoding="latin-1").read()
    except OSError:
        continue
    for name in inc_re.findall(text):
        norm = name.replace("\\", "/")
        # já resolve relativo ao próprio arquivo? ('\\' nunca resolve no Linux)
        if "\\" not in name and os.path.exists(os.path.join(os.path.dirname(sf), norm)):
            continue
        base = os.path.basename(norm).lower()
        if base not in index:
            continue
        exact = os.path.join(os.path.dirname(sf), norm)
        target = exact if os.path.exists(exact) else pick(index[base])
        if "\\" in name:
            link = os.path.join(out, name)          # nome com '\' literal
        else:
            link = os.path.join(out, norm)
            # se o nome exato já existe num dir de include, não precisa
            if os.path.basename(target) == os.path.basename(norm):
                continue
        os.makedirs(os.path.dirname(link), exist_ok=True)
        if not os.path.lexists(link):
            os.symlink(target, link)
            made += 1

if len(sys.argv) > 3:
    open(sys.argv[3], "w").write(str(made))
print(f"{made} shims de include criados em {out}")
