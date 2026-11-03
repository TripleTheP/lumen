#include "common.h"
#include <shlobj.h>
#include <cstdarg>
#include <cwctype>
#include <algorithm>

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) a++;
    while (b > a && iswspace(s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool EndsWithI(const std::wstring& s, const std::wstring& suffix) {
    if (suffix.size() > s.size()) return false;
    return ToLower(s.substr(s.size() - suffix.size())) == ToLower(suffix);
}

std::wstring FileExtLower(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    return ToLower(path.substr(dot));
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring FileStem(const std::wstring& path) {
    std::wstring n = FileNameOf(path);
    size_t dot = n.find_last_of(L'.');
    return dot == std::wstring::npos ? n : n.substr(0, dot);
}

std::wstring DirOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void EnsureDir(const std::wstring& p) {
    SHCreateDirectoryExW(nullptr, p.c_str(), nullptr);
}

bool ReadTextFile(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart > 16 * 1024 * 1024) { CloseHandle(f); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    BOOL ok = sz.QuadPart == 0 || ReadFile(f, &out[0], (DWORD)sz.QuadPart, &rd, nullptr);
    CloseHandle(f);
    if (!ok) return false;
    out.resize(rd);
    // UTF-8 BOM entfernen
    if (out.size() >= 3 && (uint8_t)out[0] == 0xEF && (uint8_t)out[1] == 0xBB && (uint8_t)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = data.empty() || WriteFile(f, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}

std::wstring AppDataDir() {
    static std::wstring dir;
    if (dir.empty()) {
        dir = JoinPath(KnownFolder(FOLDERID_RoamingAppData), APP_NAME);
        EnsureDir(dir);
    }
    return dir;
}

std::wstring LibraryDir() {
    static std::wstring dir;
    if (dir.empty()) {
        dir = JoinPath(JoinPath(KnownFolder(FOLDERID_LocalAppData), APP_NAME), L"Bibliothek");
        EnsureDir(dir);
    }
    return dir;
}

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return std::wstring(buf, n);
}

// ---- Log -------------------------------------------------------------------
static FILE* g_log = nullptr;
static CRITICAL_SECTION g_logCs;

void LogInit() {
    InitializeCriticalSection(&g_logCs);
    std::wstring p = JoinPath(AppDataDir(), L"lumen.log");
    // Alte Logs klein halten: bei > 1 MB neu beginnen
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    const wchar_t* mode = L"ab";
    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 1024 * 1024) mode = L"wb";
    g_log = _wfopen(p.c_str(), mode);
}

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap);
    buf[2047] = 0;
    va_end(ap);
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
    if (!g_log) return;
    EnterCriticalSection(&g_logCs);
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::string line = WideToUtf8(buf);
    fprintf(g_log, "%04d-%02d-%02d %02d:%02d:%02d  %s\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
            st.wSecond, line.c_str());
    fflush(g_log);
    LeaveCriticalSection(&g_logCs);
}

double NowSeconds() {
    static LARGE_INTEGER freq{}, start{};
    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart - start.QuadPart) / double(freq.QuadPart);
}
