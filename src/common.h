// Lumen – schlanke Live-Hintergründe für Windows
// Gemeinsame Includes und Hilfsfunktionen
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <cstdint>
#include <cstdio>

using Microsoft::WRL::ComPtr;

#define APP_NAME L"Lumen"
#define APP_VERSION L"0.1.0"

// ---- Strings ---------------------------------------------------------------
uint64_t Fnv1a(const std::string& s);                  // schnelle Prüfsumme (Cache-Schlüssel, Dateivergleich)
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);
std::wstring ToLower(std::wstring s);
std::wstring Trim(const std::wstring& s);
bool EndsWithI(const std::wstring& s, const std::wstring& suffix);
std::wstring FileExtLower(const std::wstring& path);   // ".mp4"
std::wstring FileNameOf(const std::wstring& path);     // "a.mp4"
std::wstring FileStem(const std::wstring& path);       // "a"
std::wstring DirOf(const std::wstring& path);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& p);
bool DirExists(const std::wstring& p);
void EnsureDir(const std::wstring& p);
bool ReadTextFile(const std::wstring& path, std::string& out);
bool WriteTextFile(const std::wstring& path, const std::string& data);

// ---- Pfade -----------------------------------------------------------------
std::wstring AppDataDir();     // %APPDATA%\Lumen
std::wstring LibraryDir();     // %LOCALAPPDATA%\Lumen\Bibliothek
std::wstring ExePath();

// ---- Log -------------------------------------------------------------------
void LogInit();
void Log(const wchar_t* fmt, ...);

// ---- Zeit ------------------------------------------------------------------
double NowSeconds();           // monotone Sekunden seit Programmstart
