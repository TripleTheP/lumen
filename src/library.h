#pragma once
#include "common.h"

enum class WallKind { Image, Video, Shader };

struct Wallpaper {
    std::wstring id;          // eindeutig, z. B. "Beispiele\Aurora" oder "we:123456"
    std::wstring name;        // Anzeigename
    std::wstring group;       // Gruppe im Menü (Unterordner der Bibliothek)
    WallKind kind = WallKind::Shader;
    std::wstring source;      // Bild/Video (absoluter Pfad) oder leer
    std::wstring shaderPath;  // eigener Shader (absoluter Pfad) oder leer
    int fit = 0;              // 0 cover, 1 contain, 2 stretch
    int fps = 0;              // 0 = globale Einstellung
    double speed = 1.0;
    bool fromWallpaperEngine = false;
    std::wstring iniPath;                          // wallpaper.ini oder leer (lose Dateien, Wallpaper Engine)
    std::map<std::wstring, std::wstring> ini;      // Inhalt der wallpaper.ini, Schlüssel kleingeschrieben
};

// key=value-Zeilen; # und ; am Zeilenanfang sind Kommentare, ein ";" nach Leerraum beendet den Wert
std::map<std::wstring, std::wstring> ParseIni(const std::string& text);

// Setzt key=value in der ini-Datei: ersetzt die Zeile des Schlüssels (Groß-/Kleinschreibung egal) oder hängt sie an.
// Alle anderen Zeilen, Kommentare und die Zeilenenden bleiben unverändert. False, wenn die Datei nicht geschrieben werden kann.
std::string SetIniValueInText(const std::string& text, const std::wstring& key, const std::wstring& value);
bool SetIniValue(const std::wstring& path, const std::wstring& key, const std::wstring& value);

class Library {
public:
    void Scan(bool includeWallpaperEngine);
    const std::vector<Wallpaper>& Items() const { return items_; }
    const Wallpaper* Find(const std::wstring& id) const;

    // Schreibt mitgelieferte Beispiele, falls sie fehlen
    void WriteExamples();
    // Kopiert eine Datei in die Bibliothek (Ordner "Eigene") und gibt ihre id zurück
    bool AddFile(const std::wstring& path, std::wstring& newId, std::wstring& err);
    // Legt einen neuen Shader aus Vorlage an
    bool CreateShaderTemplate(std::wstring& newId, std::wstring& shaderFile);

private:
    void ScanDir(const std::wstring& dir, const std::wstring& rel, int depth);
    void ScanWallpaperEngine();
    std::vector<Wallpaper> items_;
};

bool IsImageFile(const std::wstring& p);
bool IsVideoFile(const std::wstring& p);
std::wstring SanitizeFileName(const std::wstring& s);

// ---- Online-Katalog ---------------------------------------------------------
struct CatalogItem {
    std::wstring name;
    std::wstring url;
    std::wstring description;
};

// Format: eine Zeile pro Eintrag "Name | URL | Beschreibung", # für Kommentare
bool FetchCatalog(const std::wstring& url, std::vector<CatalogItem>& out, std::wstring& err);
// Lädt Eintrag herunter (Zip wird entpackt) und gibt den Bibliotheks-Ordner zurück
bool InstallCatalogItem(const CatalogItem& item, std::wstring& installedRel, std::wstring& err);
