#include "config.h"
#include <algorithm>
#include <sstream>

std::wstring Config::Path() const { return JoinPath(AppDataDir(), L"config.ini"); }

void Config::Load() {
    kv_.clear();
    std::string data;
    if (!ReadTextFile(Path(), data)) return;
    std::wstring w = Utf8ToWide(data);
    std::wstringstream ss(w);
    std::wstring line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';' || line[0] == L'[') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        kv_[ToLower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
    }
}

void Config::Save() const {
    std::wstring out = L"# Lumen Einstellungen – wird vom Programm geschrieben, darf aber von Hand bearbeitet werden.\r\n";
    for (auto& [k, v] : kv_) out += k + L"=" + v + L"\r\n";
    WriteTextFile(Path(), WideToUtf8(out));
}

std::wstring Config::Get(const std::wstring& key, const std::wstring& def) const {
    auto it = kv_.find(ToLower(key));
    return it == kv_.end() ? def : it->second;
}

int Config::GetInt(const std::wstring& key, int def) const {
    std::wstring v = Get(key);
    if (v.empty()) return def;
    return _wtoi(v.c_str());
}

double Config::GetFloat(const std::wstring& key, double def) const {
    std::wstring v = Get(key);
    if (v.empty()) return def;
    std::replace(v.begin(), v.end(), L',', L'.');
    return _wtof(v.c_str());
}

void Config::Set(const std::wstring& key, const std::wstring& value) { kv_[ToLower(key)] = value; }
void Config::Remove(const std::wstring& key) { kv_.erase(ToLower(key)); }

int Config::Fps() const {
    int f = GetInt(L"fps", 30);
    return std::clamp(f, 5, 240);
}

double Config::RenderScale() const {
    double s = GetFloat(L"render_scale", 1.0);
    return std::clamp(s, 0.25, 1.0);
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool AutostartEnabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    bool on = RegQueryValueExW(k, APP_NAME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring cmd = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, APP_NAME);
    }
    RegCloseKey(k);
}
