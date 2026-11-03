/*
FxSound for Linux - Win32 compatibility layer
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Este header é incluído à força (-include) em todas as unidades de compilação
do DSP original. Ele fornece apenas a superfície mínima da API Win32 / CRT da
Microsoft que o código usa, para que os algoritmos possam ser compilados sem
alterações no Linux.
*/
#ifndef FX_COMPAT_H
#define FX_COMPAT_H

#ifndef __linux__
#error "fx_compat.h só deve ser usado em builds Linux"
#endif

#ifdef __cplusplus
/* <cwchar> e afins fazem #undef de swprintf/fwprintf/etc. Incluí-los antes
   garante que as macros abaixo sobrevivam (os include guards impedem que o
   #undef rode de novo). */
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <atomic>
#endif
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <malloc.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- tipos */
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint32_t UINT;
typedef int64_t LONGLONG;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef void *HANDLE;
typedef void *HWND;
typedef void *HINSTANCE;
typedef void *HMODULE;
typedef void *HKEY;
typedef void *LPVOID;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef wchar_t TCHAR;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef wchar_t *LPWSTR;
typedef const wchar_t *LPCWSTR;
typedef const wchar_t *LPCTSTR;
typedef wchar_t *LPTSTR;
typedef char *HPSTR;
typedef void *LPSECURITY_ATTRIBUTES;
typedef DWORD *LPDWORD;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define WINAPI
#define CALLBACK
#define __stdcall
#define __cdecl
#define _stdcall
#define _cdecl
#define __declspec(x)
#define MAX_PATH 4096
#define _MAX_PATH 4096

/* ---------------------------------------- tipos de mmsystem (só p/ parse) */
/* Usados apenas por declarações em u_pwav.h; o player WAV do Windows
   (Pwav.cpp/PwavIO.cpp) não é compilado no Linux. */
typedef void *HWAVEIN;
typedef void *HWAVEOUT;
typedef void *HMMIO;
typedef void *HGLOBAL;
typedef void *HLOCAL;
typedef struct tWAVEFORMATEX {
    WORD wFormatTag; WORD nChannels; DWORD nSamplesPerSec; DWORD nAvgBytesPerSec;
    WORD nBlockAlign; WORD wBitsPerSample; WORD cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;
typedef struct wavehdr_tag {
    char *lpData; DWORD dwBufferLength; DWORD dwBytesRecorded; uintptr_t dwUser;
    DWORD dwFlags; DWORD dwLoops; struct wavehdr_tag *lpNext; uintptr_t reserved;
} WAVEHDR, *LPWAVEHDR;
typedef struct _MMCKINFO {
    DWORD ckid; DWORD cksize; DWORD fccType; DWORD dwDataOffset; DWORD dwFlags;
} MMCKINFO;
#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_IEEE_FLOAT 3

#define FreeLibrary(h) ((void)(h), TRUE)

/* ------------------------------------------------- caixas de mensagem */
#define MB_OK 0
#define MB_ICONERROR 0x10
#define MB_ICONWARNING 0x30
#define MB_ICONINFORMATION 0x40
#define MB_YESNO 4
#define MB_TOPMOST 0x40000
#define IDOK 1
#define IDYES 6
#define IDNO 7

#ifdef __cplusplus
extern "C" {
#endif

int fx_MessageBoxA(HWND, const char *text, const char *caption, UINT type);
int fx_MessageBoxW(HWND, const wchar_t *text, const wchar_t *caption, UINT type);
BOOL fx_QueryPerformanceCounter(LARGE_INTEGER *);
BOOL fx_QueryPerformanceFrequency(LARGE_INTEGER *);
DWORD fx_GetTickCount(void);

/* Conversão de formato printf/scanf wide do MSVC para o padrão C99/glibc:
   no MSVC, %s/%c em funções wide significam string/char wide; no glibc
   significam narrow. %S/%C invertem. */
const wchar_t *fx_wfmt(const wchar_t *fmt, wchar_t *buf, size_t buflen);

int fx_vswprintf_ms(wchar_t *buf, size_t n, const wchar_t *fmt, va_list ap);
int fx_fwprintf_ms(FILE *f, const wchar_t *fmt, ...);
wchar_t *fx_fgetws(wchar_t *buf, int n, FILE *f);
int fx_fputws(const wchar_t *s, FILE *f);
int fx_swscanf_ms(const wchar_t *s, const wchar_t *fmt, ...);
int fx_wfopen_s(FILE **f, const wchar_t *path, const wchar_t *mode);
FILE *fx_wfopen(const wchar_t *path, const wchar_t *mode);
int fx_wremove(const wchar_t *path);
int fx_wrename(const wchar_t *from, const wchar_t *to);

#ifdef __cplusplus
}
#endif

#define MessageBoxA fx_MessageBoxA
#define MessageBoxW fx_MessageBoxW
#define MessageBox fx_MessageBoxW
#define QueryPerformanceCounter fx_QueryPerformanceCounter
#define QueryPerformanceFrequency fx_QueryPerformanceFrequency
#define GetTickCount fx_GetTickCount
#define Sleep(ms) usleep((ms) * 1000)

/* -------------------------------------------- CRT da Microsoft (wide) */
#define _wtoi(s) ((int)wcstol((s), NULL, 10))
#define _wtol(s) wcstol((s), NULL, 10)
#define _wtof(s) wcstod((s), NULL)
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define wcsicmp wcscasecmp
#define _stricmp strcasecmp
#define stricmp strcasecmp
#define _strnicmp strncasecmp
#define strnicmp strncasecmp
#define _wfopen_s fx_wfopen_s
#define _wfopen fx_wfopen
#define _wremove fx_wremove
#define _wrename fx_wrename
/* Streams: o código original mistura fgets/fprintf com fgetws/fwprintf no
   mesmo FILE*. No glibc um stream tem orientação única (byte OU wide), então
   as variantes wide aqui operam sobre bytes UTF-8, como o MSVC em modo texto. */
#define fwprintf fx_fwprintf_ms
#define fgetws fx_fgetws
#define fputws fx_fputws
#define swscanf fx_swscanf_ms
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define sprintf_s snprintf
#define strcpy_s(d, n, s) (strncpy((d), (s), (n)), (d)[(n) - 1] = 0, 0)
#define wcscpy_s(d, n, s) (wcsncpy((d), (s), (n)), (d)[(n) - 1] = 0, 0)
#define wcscat_s(d, n, s) (wcsncat((d), (s), (n) - wcslen(d) - 1), 0)
#define _itow(v, buf, radix) (swprintf((buf), 34, L"%d", (int)(v)), (buf))

#ifdef __cplusplus
#include <cstdarg>
/* swprintf do MSVC aceita a forma legada (buf, fmt, ...) e a forma padrão
   (buf, count, fmt, ...); ambas com semântica de formato MS. */
static inline int fx_swprintf_ms(wchar_t *buf, size_t n, const wchar_t *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = fx_vswprintf_ms(buf, n, fmt, ap);
    va_end(ap); return r;
}
static inline int fx_swprintf_ms(wchar_t *buf, const wchar_t *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    /* forma legada: sem limite conhecido; usa um limite generoso */
    int r = fx_vswprintf_ms(buf, 4096, fmt, ap);
    va_end(ap); return r;
}
#define swprintf fx_swprintf_ms
#endif

/* ------------------------------------------------ modelo de dados LLP64
 * O DSP foi escrito para Windows, onde 'long' tem 32 bits mesmo em 64-bit
 * (LLP64). Ele usa arrays de long como "memória DSP" de 32 bits, com floats
 * e longs compartilhando os mesmos slots. No Linux (LP64) long tem 64 bits,
 * o que corrompe essa memória. Reproduzimos o modelo LLP64 apenas nas
 * unidades do DSP. Todos os headers de sistema precisam ter sido incluídos
 * acima deste ponto.
 * __int64 é definido sem a palavra 'long' para sobreviver à macro e ainda
 * aceitar 'unsigned __int64'. */
#define __int64 __attribute__((mode(DI))) int
#define long int

#endif /* FX_COMPAT_H */
