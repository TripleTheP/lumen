#include "library.h"
#include "examples.h"
#include <urlmon.h>
#include <wininet.h>
#include <shellapi.h>
#include <algorithm>
#include <sstream>

static const wchar_t* kImageExt[] = {L".jpg", L".jpeg", L".png", L".bmp", L".webp", L".gif", L".tif", L".tiff", L".jxr", L".heic"};
static const wchar_t* kVideoExt[] = {L".mp4", L".webm", L".mkv", L".mov", L".wmv", L".avi", L".m4v"};

bool IsImageFile(const std::wstring& p) {
    std::wstring e = FileExtLower(p);
    for (auto x : kImageExt) if (e == x) return true;
    return false;
}

bool IsVideoFile(const std::wstring& p) {
    std::wstring e = FileExtLower(p);
    for (auto x : kVideoExt) if (e == x) return true;
    return false;
}

std::wstring SanitizeFileName(const std::wstring& s) {
    std::wstring r;
    for (wchar_t c : s) {
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c)) r += L'_';
        else r += c;
    }
    r = Trim(r);
    while (!r.empty() && (r.back() == L'.' || r.back() == L' ')) r.pop_back();
    if (r.size() > 80) r.resize(80);
    if (r.empty()) r = L"Hintergrund";
    return r;
}

static std::map<std::wstring, std::wstring> ParseIni(const std::string& text) {
    std::map<std::wstring, std::wstring> kv;
    std::wstringstream ss(Utf8ToWide(text));
    std::wstring line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';' || line[0] == L'[') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        kv[ToLower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
    }
    return kv;
}

static std::wstring ResolveIn(const std::wstring& dir, const std::wstring& p) {
    if (p.empty()) return L"";
    if (p.size() > 1 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\'))) return p;
    std::wstring q = p;
    std::replace(q.begin(), q.end(), L'/', L'\\');
    return JoinPath(dir, q);
}

static std::wstring TopGroup(const std::wstring& rel) {
    size_t s = rel.find(L'\\');
    return s == std::wstring::npos ? L"" : rel.substr(0, s);
}

static int FitFromString(const std::wstring& s) {
    std::wstring l = ToLower(s);
    if (l == L"contain" || l == L"einpassen") return 1;
    if (l == L"stretch" || l == L"strecken") return 2;
    return 0;
}

void Library::Scan(bool includeWallpaperEngine) {
    items_.clear();
    ScanDir(LibraryDir(), L"", 0);
    std::sort(items_.begin(), items_.end(), [](const Wallpaper& a, const Wallpaper& b) {
        if (a.group != b.group) return ToLower(a.group) < ToLower(b.group);
        return ToLower(a.name) < ToLower(b.name);
    });
    if (includeWallpaperEngine) ScanWallpaperEngine();
    Log(L"Bibliothek: %d Hintergründe", (int)items_.size());
}

const Wallpaper* Library::Find(const std::wstring& id) const {
    for (auto& w : items_)
        if (ToLower(w.id) == ToLower(id)) return &w;
    return nullptr;
}

void Library::ScanDir(const std::wstring& dir, const std::wstring& rel, int depth) {
    std::wstring ini = JoinPath(dir, L"wallpaper.ini");
    if (!rel.empty() && FileExists(ini)) {
        std::string text;
        ReadTextFile(ini, text);
        auto kv = ParseIni(text);
        Wallpaper w;
        w.id = rel;
        w.group = TopGroup(rel);
        w.name = kv.count(L"name") ? kv[L"name"] : FileNameOf(rel);
        std::wstring src = kv.count(L"source") ? kv[L"source"] : (kv.count(L"video") ? kv[L"video"] : kv[L"image"]);
        w.source = ResolveIn(dir, src);
        if (kv.count(L"shader")) w.shaderPath = ResolveIn(dir, kv[L"shader"]);
        w.fit = FitFromString(kv[L"fit"]);
        w.fps = kv.count(L"fps") ? _wtoi(kv[L"fps"].c_str()) : 0;
        if (kv.count(L"speed")) {
            std::wstring s = kv[L"speed"];
            std::replace(s.begin(), s.end(), L',', L'.');
            w.speed = _wtof(s.c_str());
            if (w.speed <= 0) w.speed = 1.0;
        }
        if (!w.source.empty() && IsVideoFile(w.source)) w.kind = WallKind::Video;
        else if (!w.source.empty()) w.kind = WallKind::Image;
        else w.kind = WallKind::Shader;
        bool ok = (w.kind == WallKind::Shader) ? FileExists(w.shaderPath) : FileExists(w.source);
        if (ok) items_.push_back(w);
        else Log(L"Bibliothek: %s unvollständig (Datei fehlt)", rel.c_str());
        return;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        std::wstring full = JoinPath(dir, n);
        std::wstring r = rel.empty() ? n : rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 3) ScanDir(full, r, depth + 1);
            continue;
        }
        Wallpaper w;
        w.id = r;
        w.group = TopGroup(r);
        w.name = FileStem(n);
        if (IsVideoFile(n)) { w.kind = WallKind::Video; w.source = full; }
        else if (IsImageFile(n)) { w.kind = WallKind::Image; w.source = full; }
        else if (FileExtLower(n) == L".hlsl" && !FileExists(JoinPath(dir, L"wallpaper.ini"))) {
            w.kind = WallKind::Shader;
            w.shaderPath = full;
        } else continue;
        items_.push_back(w);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// ---- Wallpaper Engine (nur Video-Hintergründe aus dem Workshop) --------------------
static std::wstring JsonString(const std::string& json, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return L"";
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return L"";
    p = json.find('"', p);
    if (p == std::string::npos) return L"";
    std::wstring out;
    std::string chunk;
    for (size_t i = p + 1; i < json.size(); i++) {
        char c = json[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < json.size()) {
            char n = json[++i];
            if (n == 'u' && i + 4 < json.size()) {
                out += Utf8ToWide(chunk);
                chunk.clear();
                out += (wchar_t)strtol(json.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
            } else if (n == 'n') chunk += ' ';
            else chunk += n;
        } else chunk += c;
    }
    out += Utf8ToWide(chunk);
    return out;
}

void Library::ScanWallpaperEngine() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS)
        return;
    std::wstring steam = buf;
    std::replace(steam.begin(), steam.end(), L'/', L'\\');
    std::vector<std::wstring> libs = {steam};
    std::string vdf;
    if (ReadTextFile(JoinPath(steam, L"steamapps\\libraryfolders.vdf"), vdf)) {
        std::istringstream ss(vdf);
        std::string line;
        while (std::getline(ss, line)) {
            size_t k = line.find("\"path\"");
            if (k == std::string::npos) continue;
            size_t a = line.find('"', k + 6);
            size_t b = line.rfind('"');
            if (a == std::string::npos || b <= a) continue;
            std::string p = line.substr(a + 1, b - a - 1);
            std::string u;
            for (size_t i = 0; i < p.size(); i++) {
                if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') i++;
                u += p[i];
            }
            std::wstring w = Utf8ToWide(u);
            if (std::none_of(libs.begin(), libs.end(), [&](const std::wstring& x) { return ToLower(x) == ToLower(w); }))
                libs.push_back(w);
        }
    }
    int found = 0;
    for (auto& lib : libs) {
        std::wstring ws = JoinPath(lib, L"steamapps\\workshop\\content\\431960");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(JoinPath(ws, L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            std::wstring dir = JoinPath(ws, fd.cFileName);
            std::string pj;
            if (!ReadTextFile(JoinPath(dir, L"project.json"), pj)) continue;
            std::wstring type = ToLower(JsonString(pj, "type"));
            std::wstring file = JsonString(pj, "file");
            if (type != L"video" || file.empty()) continue;   // Szenen und Web-Hintergründe kann Lumen nicht
            std::wstring src = ResolveIn(dir, file);
            if (!FileExists(src)) continue;
            Wallpaper w;
            w.id = std::wstring(L"we:") + fd.cFileName;
            w.name = JsonString(pj, "title");
            if (w.name.empty()) w.name = fd.cFileName;
            w.group = L"Wallpaper Engine";
            w.kind = WallKind::Video;
            w.source = src;
            w.fromWallpaperEngine = true;
            items_.push_back(w);
            found++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (found) Log(L"Wallpaper Engine: %d Video-Hintergründe gefunden", found);
}

// ---- Beispiele und Vorlagen ---------------------------------------------------------
void Library::WriteExamples() {
    std::wstring base = JoinPath(LibraryDir(), L"Beispiele");
    for (auto& ex : Examples()) {
        std::wstring dir = JoinPath(base, ex.folder);
        if (FileExists(JoinPath(dir, L"wallpaper.ini"))) {
            // Unveränderte Kopie der Vorgängerfassung auffrischen – selbst bearbeitete Beispiele bleiben unangetastet
            std::wstring sh = JoinPath(dir, L"shader.hlsl");
            std::string cur;
            if (ex.replaces && ReadTextFile(sh, cur) && Fnv1a(cur) == ex.replaces) {
                WriteTextFile(sh, ex.shader);
                Log(L"Beispiel aktualisiert: %s", ex.folder);
            }
            continue;
        }
        EnsureDir(dir);
        WriteTextFile(JoinPath(dir, L"wallpaper.ini"), ex.ini);
        WriteTextFile(JoinPath(dir, L"shader.hlsl"), ex.shader);
    }
    WriteTextFile(JoinPath(LibraryDir(), L"LIESMICH.txt"), LibraryReadme());
}

static std::wstring UniquePath(const std::wstring& dir, const std::wstring& stem, const std::wstring& ext) {
    std::wstring p = JoinPath(dir, stem + ext);
    for (int i = 2; FileExists(p) || DirExists(p); i++) p = JoinPath(dir, stem + L" (" + std::to_wstring(i) + L")" + ext);
    return p;
}

bool Library::AddFile(const std::wstring& path, std::wstring& newId, std::wstring& err) {
    if (!IsImageFile(path) && !IsVideoFile(path) && FileExtLower(path) != L".hlsl") {
        err = L"Dateityp wird nicht unterstützt";
        return false;
    }
    std::wstring dir = JoinPath(LibraryDir(), L"Eigene");
    EnsureDir(dir);
    std::wstring dest = UniquePath(dir, SanitizeFileName(FileStem(path)), FileExtLower(path));
    if (!CopyFileW(path.c_str(), dest.c_str(), TRUE)) {
        err = L"Datei konnte nicht kopiert werden";
        return false;
    }
    newId = L"Eigene\\" + FileNameOf(dest);
    return true;
}

bool Library::CreateShaderTemplate(std::wstring& newId, std::wstring& shaderFile) {
    std::wstring parent = JoinPath(LibraryDir(), L"Eigene");
    EnsureDir(parent);
    std::wstring dir = UniquePath(parent, L"Mein Shader", L"");
    EnsureDir(dir);
    std::wstring name = FileNameOf(dir);
    WriteTextFile(JoinPath(dir, L"wallpaper.ini"), "name=" + WideToUtf8(name) + "\nshader=shader.hlsl\n");
    shaderFile = JoinPath(dir, L"shader.hlsl");
    WriteTextFile(shaderFile, ShaderTemplate());
    newId = L"Eigene\\" + name;
    return true;
}

// ---- Online-Katalog -----------------------------------------------------------------
static bool Download(const std::wstring& url, const std::wstring& dest, std::wstring& err) {
    DeleteUrlCacheEntryW(url.c_str());
    HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), dest.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        wchar_t b[64];
        swprintf(b, 64, L"Download fehlgeschlagen (0x%08X)", (unsigned)hr);
        err = b;
        return false;
    }
    return true;
}

static std::wstring TempFile(const std::wstring& ext) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + L"lumen_" + std::to_wstring(GetTickCount64()) + ext;
}

static bool IsHttps(const std::wstring& u) { return ToLower(u.substr(0, 8)) == L"https://"; }

// Ordner samt Inhalt endgültig löschen, ohne Dialoge (std::filesystem würde die exe um ~200 KB vergrößern)
static void RemoveTree(const std::wstring& dir) {
    if (!DirExists(dir)) return;
    std::wstring from = dir + L'\0';   // pFrom muss doppelt nullterminiert sein
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

bool FetchCatalog(const std::wstring& url, std::vector<CatalogItem>& out, std::wstring& err) {
    out.clear();
    if (!IsHttps(url)) {
        err = L"Katalog-Adresse muss mit https:// beginnen";
        return false;
    }
    std::wstring tmp = TempFile(L".txt");
    if (!Download(url, tmp, err)) return false;
    std::string text;
    ReadTextFile(tmp, text);
    DeleteFileW(tmp.c_str());
    std::wstringstream ss(Utf8ToWide(text));
    std::wstring line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#') continue;
        std::vector<std::wstring> parts;
        size_t a = 0;
        while (true) {
            size_t b = line.find(L'|', a);
            parts.push_back(Trim(line.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a)));
            if (b == std::wstring::npos) break;
            a = b + 1;
        }
        if (parts.size() < 2 || parts[0].empty() || !IsHttps(parts[1])) continue;
        out.push_back({parts[0], parts[1], parts.size() > 2 ? parts[2] : L""});
        if (out.size() >= 500) break;
    }
    if (out.empty()) {
        err = L"Katalog ist leer oder hat ein falsches Format";
        return false;
    }
    return true;
}

bool InstallCatalogItem(const CatalogItem& item, std::wstring& installedRel, std::wstring& err) {
    std::wstring path = item.url.substr(0, item.url.find_first_of(L"?#"));
    std::wstring ext = FileExtLower(path);
    std::wstring name = SanitizeFileName(item.name);
    std::wstring online = JoinPath(LibraryDir(), L"Online");
    EnsureDir(online);
    std::wstring tmp = TempFile(ext.empty() ? L".bin" : ext);
    if (!Download(item.url, tmp, err)) return false;

    if (ext == L".zip") {
        // Erst außerhalb der Bibliothek entpacken (gleiches Laufwerk), dann den alten Stand als Ganzes ersetzen
        std::wstring dest = JoinPath(online, name);
        std::wstring work = JoinPath(DirOf(LibraryDir()), L"Download");
        std::wstring stage = JoinPath(work, name), old = JoinPath(work, name + L".alt");
        RemoveTree(stage);   // Reste eines abgebrochenen Downloads
        RemoveTree(old);
        EnsureDir(stage);
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        std::wstring tar = JoinPath(sys, L"tar.exe");
        std::wstring cmd = L"\"" + tar + L"\" -xf \"" + tmp + L"\" -C \"" + stage + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(tar.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            DeleteFileW(tmp.c_str());
            err = L"Entpacken nicht möglich (tar.exe fehlt?)";
            return false;
        }
        WaitForSingleObject(pi.hProcess, 120000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        DeleteFileW(tmp.c_str());
        if (code != 0) {
            RemoveTree(stage);
            err = L"Zip-Datei konnte nicht entpackt werden";
            return false;
        }
        // Umbenennen scheitert, solange etwas darin offen ist (laufendes Video, Explorer) – dann bleibt alles, wie es war
        if (DirExists(dest) && !MoveFileExW(dest.c_str(), old.c_str(), 0)) {
            RemoveTree(stage);
            err = L"Der Ordner wird gerade benutzt (angezeigt oder im Explorer geöffnet). Bitte schließen bzw. einen "
                  L"anderen Hintergrund wählen und erneut installieren.";
            return false;
        }
        if (!MoveFileExW(stage.c_str(), dest.c_str(), 0)) {
            if (DirExists(old)) MoveFileExW(old.c_str(), dest.c_str(), 0);
            RemoveTree(stage);
            err = L"Entpackte Dateien konnten nicht in die Bibliothek verschoben werden";
            return false;
        }
        RemoveTree(old);
        installedRel = L"Online\\" + name;
        return true;
    }
    if (IsImageFile(path) || IsVideoFile(path) || ext == L".hlsl") {
        std::wstring dest = JoinPath(online, name + ext);
        if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            DeleteFileW(tmp.c_str());
            err = L"Datei konnte nicht gespeichert werden";
            return false;
        }
        installedRel = L"Online\\" + name + ext;
        return true;
    }
    DeleteFileW(tmp.c_str());
    err = L"Dateityp im Katalog wird nicht unterstützt";
    return false;
}
