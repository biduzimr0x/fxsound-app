/*
FxSound for Linux - Win32 compatibility layer (implementação)
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Implementa:
  - o subconjunto da API Win32/CRT MS declarado em fx_compat.h;
  - substitutos Linux para os utilitários do audiopassthru que o DSP usa e
    que dependem do Windows (registro, arquivos, conversões UTF-8).
As funções matemáticas (MTH) e o CSlout são compilados a partir dos fontes
originais.
*/
#include "fx_compat.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

#include "codedefs.h"
#include "slout.h"
#include "reg.h"
#include "pstr.h"
#include "File.h"

/* ================================================================ util */

static std::string fx_utf8(const wchar_t *ws)
{
    std::string out;
    if (!ws) return out;
    for (; *ws; ++ws) {
        uint32_t c = (uint32_t)*ws;
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        } else {
            out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F));
            out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

static std::wstring fx_wide(const char *s)
{
    std::wstring out;
    if (!s) return out;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c = *p++;
        int extra = 0;
        if (c >= 0xF0) { c &= 0x07; extra = 3; }
        else if (c >= 0xE0) { c &= 0x0F; extra = 2; }
        else if (c >= 0xC0) { c &= 0x1F; extra = 1; }
        for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i) c = (c << 6) | (*p++ & 0x3F);
        out += (wchar_t)c;
    }
    return out;
}

/* Caminho Windows (wide, '\\') -> caminho Linux (UTF-8, '/'). */
static std::string fx_path(const wchar_t *wp)
{
    std::string p = fx_utf8(wp);
    for (auto &ch : p) if (ch == '\\') ch = '/';
    return p;
}

/* ============================================================ Win32 API */

extern "C" int fx_MessageBoxA(HWND, const char *text, const char *caption, UINT)
{
    fprintf(stderr, "[fxsound-dsp] %s: %s\n", caption ? caption : "", text ? text : "");
    return IDOK;
}

extern "C" int fx_MessageBoxW(HWND, const wchar_t *text, const wchar_t *caption, UINT)
{
    fprintf(stderr, "[fxsound-dsp] %s: %s\n", fx_utf8(caption).c_str(), fx_utf8(text).c_str());
    return IDOK;
}

extern "C" BOOL fx_QueryPerformanceCounter(LARGE_INTEGER *li)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    li->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}

extern "C" BOOL fx_QueryPerformanceFrequency(LARGE_INTEGER *li)
{
    li->QuadPart = 1000000000LL;
    return TRUE;
}

extern "C" DWORD fx_GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

/* ===================================== formatação wide no estilo do MSVC */

/*
 * No MSVC, dentro de funções wide (swprintf, fwprintf, swscanf...):
 *   %s %c %[  -> argumento wide        (glibc: narrow)
 *   %S %C     -> argumento narrow      (glibc: %S/%C = wide)
 * Esta função reescreve o formato para a semântica C99 do glibc.
 */
extern "C" const wchar_t *fx_wfmt(const wchar_t *fmt, wchar_t *buf, size_t buflen)
{
    size_t o = 0;
    auto put = [&](wchar_t c) { if (o + 1 < buflen) buf[o++] = c; };
    for (const wchar_t *p = fmt; *p; ++p) {
        if (*p != L'%') { put(*p); continue; }
        put(*p++);
        if (*p == L'%') { put(*p); continue; }
        /* flags, largura, precisão, '*' */
        while (*p && wcschr(L"-+ #0123456789.*'", *p)) put(*p++);
        /* modificadores de tamanho */
        bool has_len = false;
        while (*p && wcschr(L"hlLqjztI", *p)) {
            has_len = true;
            /* 'long' é int nas unidades do DSP (LLP64): %ld -> %d */
            if (*p == L'l' && p[1] && wcschr(L"diouxX", p[1])) { ++p; continue; }
            /* I64/I32 do MSVC */
            if (*p == L'I') {
                if (p[1] == L'6' && p[2] == L'4') { put(L'l'); put(L'l'); p += 3; continue; }
                if (p[1] == L'3' && p[2] == L'2') { p += 3; continue; }
                put(L'z'); ++p; continue;
            }
            put(*p++);
        }
        if (!*p) break;
        wchar_t conv = *p;
        if (!has_len && (conv == L's' || conv == L'c' || conv == L'[')) {
            put(L'l'); put(conv);
        } else if (!has_len && (conv == L'S' || conv == L'C')) {
            put(conv == L'S' ? L's' : L'c');
        } else {
            put(conv);
        }
        if (conv == L'[') { /* copia o conjunto até ']' */
            ++p;
            if (*p == L'^') put(*p++);
            if (*p == L']') put(*p++);
            while (*p && *p != L']') put(*p++);
            if (*p) put(*p); else break;
        }
    }
    buf[o < buflen ? o : buflen - 1] = 0;
    return buf;
}

#define FX_FMT_BUF 2048

extern "C" int fx_vswprintf_ms(wchar_t *buf, size_t n, const wchar_t *fmt, va_list ap)
{
    wchar_t f[FX_FMT_BUF];
    int r = vswprintf(buf, n, fx_wfmt(fmt, f, FX_FMT_BUF), ap);
    if (r < 0 && n > 0) buf[n - 1] = 0; /* MSVC trunca em vez de falhar */
    return r;
}

extern "C" int fx_fwprintf_ms(FILE *fp, const wchar_t *fmt, ...)
{
    wchar_t f[FX_FMT_BUF];
    std::vector<wchar_t> out(8192);
    va_list ap; va_start(ap, fmt);
    int r = vswprintf(out.data(), out.size(), fx_wfmt(fmt, f, FX_FMT_BUF), ap);
    va_end(ap);
    if (r < 0) return r;
    return fputs(fx_utf8(out.data()).c_str(), fp) < 0 ? -1 : r;
}

extern "C" int fx_fputws(const wchar_t *s, FILE *fp)
{
    return fputs(fx_utf8(s).c_str(), fp) < 0 ? -1 : 0;
}

extern "C" wchar_t *fx_fgetws(wchar_t *buf, int n, FILE *fp)
{
    if (n <= 0) return NULL;
    std::vector<char> line((size_t)n * 4 + 1);
    if (!fgets(line.data(), (int)line.size(), fp)) return NULL;
    std::wstring w = fx_wide(line.data());
    /* normaliza CRLF de arquivos criados no Windows */
    if (w.size() >= 2 && w[w.size() - 2] == L'\r' && w.back() == L'\n') { w.pop_back(); w.back() = L'\n'; }
    size_t len = w.size() < (size_t)(n - 1) ? w.size() : (size_t)(n - 1);
    wmemcpy(buf, w.c_str(), len);
    buf[len] = 0;
    return buf;
}

extern "C" int fx_swscanf_ms(const wchar_t *s, const wchar_t *fmt, ...)
{
    wchar_t f[FX_FMT_BUF];
    va_list ap; va_start(ap, fmt);
    int r = vswscanf(s, fx_wfmt(fmt, f, FX_FMT_BUF), ap);
    va_end(ap);
    return r;
}

/* ============================================================ arquivos */

extern "C" FILE *fx_wfopen(const wchar_t *path, const wchar_t *mode)
{
    if (!path || !mode) return NULL;
    /* remove modificadores exclusivos do MSVC: ",ccs=UTF-8", 't', 'N' etc */
    std::string m;
    for (const wchar_t *p = mode; *p && *p != L','; ++p)
        if (wcschr(L"rwab+x", *p)) m += (char)*p;
    if (m.empty()) m = "r";
    return fopen(fx_path(path).c_str(), m.c_str());
}

extern "C" int fx_wfopen_s(FILE **f, const wchar_t *path, const wchar_t *mode)
{
    if (!f) return EINVAL;
    *f = fx_wfopen(path, mode);
    return *f ? 0 : (errno ? errno : ENOENT);
}

extern "C" int fx_wremove(const wchar_t *path) { return remove(fx_path(path).c_str()); }

extern "C" int fx_wrename(const wchar_t *from, const wchar_t *to)
{
    return rename(fx_path(from).c_str(), fx_path(to).c_str());
}

FILE PT_DECLSPEC *fileOpen_Wide(wchar_t *wcp_name, wchar_t *wcp_mode, CSlout *)
{
    FILE *f = fx_wfopen(wcp_name, wcp_mode);
    if (!f && wcp_name)
        fprintf(stderr, "[fxsound-dsp] não foi possível abrir %s: %s\n",
                fx_path(wcp_name).c_str(), strerror(errno));
    return f;
}

int PT_DECLSPEC fileExist_Wide(wchar_t *wcp_name, int *ip_exist)
{
    if (wcp_name == NULL) return NOT_OKAY;
    struct stat st;
    *ip_exist = (stat(fx_path(wcp_name).c_str(), &st) == 0) ? IS_TRUE : IS_FALSE;
    return OKAY;
}

int PT_DECLSPEC fileCreateDirectoryAndParents_Wide(wchar_t *wcp_path, int *ip_success, CSlout *)
{
    if (wcp_path == NULL) return NOT_OKAY;
    std::string p = fx_path(wcp_path);
    *ip_success = IS_TRUE;
    for (size_t i = 1; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') {
            std::string sub = p.substr(0, i);
            if (mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) { *ip_success = IS_FALSE; break; }
        }
    }
    return OKAY;
}

/* ============================================================== strings */

int PT_DECLSPEC pstrCalcLocationOfStrInStr_Wide(wchar_t *wcp_haystack, wchar_t *wcp_needle,
                                                int i_start_search_index,
                                                int *ip_location, int *ip_found)
{
    if (wcp_haystack == NULL || wcp_needle == NULL || i_start_search_index < 0)
        return NOT_OKAY;
    *ip_found = IS_FALSE;
    *ip_location = -1;
    wchar_t *hit = wcsstr(wcp_haystack + i_start_search_index, wcp_needle);
    if (hit) { *ip_location = (int)(hit - wcp_haystack); *ip_found = IS_TRUE; }
    return OKAY;
}

int PT_DECLSPEC pstrCovertWideCharStringToUTF8String_WithAlloc(wchar_t *wcp_original_string,
                                                               char **cpp_converted_utf8_string,
                                                               int *ip_number_of_bytes)
{
    *cpp_converted_utf8_string = NULL;
    *ip_number_of_bytes = 0;
    if (!wcp_original_string) return NOT_OKAY;
    std::string s = fx_utf8(wcp_original_string);
    *cpp_converted_utf8_string = (char *)calloc(s.size() + 1, 1);
    if (!*cpp_converted_utf8_string) return NOT_OKAY;
    memcpy(*cpp_converted_utf8_string, s.c_str(), s.size() + 1);
    *ip_number_of_bytes = (int)s.size() + 1;
    return OKAY;
}

int PT_DECLSPEC pstrCovertUTF8StringToWideCharString_WithAlloc(char *cp_original_utf8_string,
                                                               wchar_t **wcpp_converted_string,
                                                               int *ip_number_of_wchar)
{
    *wcpp_converted_string = NULL;
    *ip_number_of_wchar = 0;
    if (!cp_original_utf8_string) return NOT_OKAY;
    std::wstring w = fx_wide(cp_original_utf8_string);
    *wcpp_converted_string = (wchar_t *)calloc(w.size() + 1, sizeof(wchar_t));
    if (!*wcpp_converted_string) return NOT_OKAY;
    wmemcpy(*wcpp_converted_string, w.c_str(), w.size() + 1);
    *ip_number_of_wchar = (int)w.size() + 1;
    return OKAY;
}

/* ============================================================ registro */

/*
 * O DSP original guarda alguns estados ("último usado") no registro do
 * Windows. No Linux a aplicação persiste suas próprias configurações, então
 * aqui o registro é apenas um mapa em memória, válido durante o processo.
 */
static std::mutex g_reg_mutex;
static std::map<std::wstring, std::wstring> g_reg;

static std::wstring reg_key(int cls, const wchar_t *name)
{
    return std::to_wstring(cls) + L":" + (name ? name : L"");
}

int PT_DECLSPEC regCreateKey_Wide(int i_key_class, wchar_t *wcp_keyname, wchar_t *wcp_data)
{
    if (!wcp_keyname) return NOT_OKAY;
    std::lock_guard<std::mutex> lk(g_reg_mutex);
    g_reg[reg_key(i_key_class, wcp_keyname)] = wcp_data ? wcp_data : L"";
    return OKAY;
}

int PT_DECLSPEC regCreateKey(int i_key_class, char *cp_keyname, char *cp_data)
{
    std::wstring k = fx_wide(cp_keyname), d = fx_wide(cp_data);
    return regCreateKey_Wide(i_key_class, (wchar_t *)k.c_str(), (wchar_t *)d.c_str());
}

int PT_DECLSPEC regReadKey_Wide(int i_key_class, wchar_t *wcp_keyname, int *ip_key_exists_flag,
                                wchar_t *wcp_data, unsigned long ul_buffer_size)
{
    *ip_key_exists_flag = IS_FALSE;
    if (!wcp_keyname) return NOT_OKAY;
    std::lock_guard<std::mutex> lk(g_reg_mutex);
    auto it = g_reg.find(reg_key(i_key_class, wcp_keyname));
    if (it == g_reg.end()) return OKAY;
    if (ul_buffer_size == 0) return NOT_OKAY;
    wcsncpy(wcp_data, it->second.c_str(), ul_buffer_size - 1);
    wcp_data[ul_buffer_size - 1] = 0;
    *ip_key_exists_flag = IS_TRUE;
    return OKAY;
}

/* Conversões "ANSI" <-> wide. No Linux o "ANSI" é UTF-8. */
int PT_DECLSPEC pstrConvertToWideCharString(char *cp_string, wchar_t *wcp_string, int i_buffer_size)
{
    if (!cp_string || !wcp_string || i_buffer_size <= 0) return NOT_OKAY;
    std::wstring w = fx_wide(cp_string);
    wcsncpy(wcp_string, w.c_str(), i_buffer_size - 1);
    wcp_string[i_buffer_size - 1] = 0;
    return OKAY;
}

int PT_DECLSPEC pstrConvertWideCharStringToAnsiCharString(wchar_t *wcp_string, char *cp_string, int i_buffer_size)
{
    if (!cp_string || !wcp_string || i_buffer_size <= 0) return NOT_OKAY;
    std::string s = fx_utf8(wcp_string);
    strncpy(cp_string, s.c_str(), i_buffer_size - 1);
    cp_string[i_buffer_size - 1] = 0;
    return OKAY;
}
