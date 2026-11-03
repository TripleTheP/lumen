#pragma once
#include "common.h"

struct MonitorInfo {
    std::wstring device;  // z. B. \\.\DISPLAY1
    RECT rc{};            // ganzer Bildschirm (physische Pixel)
    RECT work{};          // ohne Taskleiste
    bool primary = false;
};

std::vector<MonitorInfo> EnumMonitors();

// Findet das Fenster hinter den Desktop-Symbolen und hängt unsere Fenster dort ein.
class DesktopHost {
public:
    bool Attach(bool layered);
    bool IsValid() const;
    void Place(HWND child, const RECT& screenRect);
    HWND Parent() const { return parent_; }
    bool Raised() const { return raised_; }
    bool Fallback() const { return fallback_; }
    bool ExplorerAppeared() const;   // im Ersatzmodus: ist der Explorer inzwischen da?

private:
    HWND progman_ = nullptr;
    HWND parent_ = nullptr;
    HWND workerw_ = nullptr;
    HWND defview_ = nullptr;
    bool raised_ = false;   // Windows 11 24H2+: WorkerW ist Kind von Progman
    bool layered_ = false;
    bool fallback_ = false;
};

// Liegt der Mauszeiger gerade über dem Desktop (nicht über einem Fenster)?
bool IsPointOverDesktop(POINT pt);

// Wird der Bildschirm von einem maximierten oder Vollbild-Fenster verdeckt?
bool IsMonitorCovered(const MonitorInfo& m);

// Läuft im Vordergrund ein Vollbild-Programm (Spiel, Video mit F11)?
bool IsForegroundFullscreen();

// Gibt Windows den normalen Hintergrund zurück (nach dem Beenden)
void RefreshSystemWallpaper();
