#pragma once
#include "common.h"

// Einfache key=value-Datei (UTF-8) unter %APPDATA%\Lumen\config.ini
class Config {
public:
    void Load();
    void Save() const;

    std::wstring Get(const std::wstring& key, const std::wstring& def = L"") const;
    int GetInt(const std::wstring& key, int def) const;
    double GetFloat(const std::wstring& key, double def) const;
    bool GetBool(const std::wstring& key, bool def) const { return GetInt(key, def ? 1 : 0) != 0; }

    void Set(const std::wstring& key, const std::wstring& value);
    void SetInt(const std::wstring& key, int v) { Set(key, std::to_wstring(v)); }
    void SetBool(const std::wstring& key, bool v) { SetInt(key, v ? 1 : 0); }
    void Remove(const std::wstring& key);

    // Komfort
    int Fps() const;
    double RenderScale() const;

private:
    std::map<std::wstring, std::wstring> kv_;
    std::wstring Path() const;
};

// Mit Windows starten (HKCU\...\Run)
bool AutostartEnabled();
void SetAutostart(bool on);
