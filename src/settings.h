#pragma once
#include "common.h"
#include "config.h"
#include "library.h"
#include "renderer.h"
#include <functional>

// Vorschaubild eines Hintergrunds (32-Bit-Bitmap, tw x th, Bildmitte; nullptr, wenn nichts erzeugt werden konnte):
// Shader werden kurz gezeichnet, Bilder skaliert, Videos über die Vorschau der Windows-Shell geholt.
HBITMAP MakeWallpaperThumbnail(Gfx& gfx, const Wallpaper& w, int tw, int th);

struct SettingsScreen {
    std::wstring label;    // "Bildschirm 1 (2560x1440)"
    std::wstring wallId;
};

struct SettingsProp {
    ShaderParam p;
    float v[4] = {};       // aktueller Wert (Zahl in [0], Farbe als rgb)
};

// Was das Fenster von der App braucht. Das Fenster ändert selbst nichts: Schalter laufen über dieselben Befehle wie das
// Tray-Menü (command), damit beide dasselbe tun.
struct SettingsContext {
    const Config* cfg = nullptr;
    Gfx* gfx = nullptr;
    std::function<void(UINT)> command;                          // Menübefehl ausführen (commands.h)
    std::function<bool(UINT)> checked;                          // Zustand eines Schalters (Menübefehl)
    std::function<std::vector<Wallpaper>()> items;              // Bibliothek neu einlesen und liefern
    std::function<std::vector<SettingsScreen>()> screens;
    std::function<void(int, const std::wstring&)> select;       // Hintergrund wählen; Bildschirm -1 = alle
    std::function<std::vector<SettingsProp>(std::wstring&)> props;   // Eigenschaften des gezeigten Hintergrunds (+ sein Name)
    std::function<void(const std::wstring&, const std::wstring&, bool)> setProp;   // Name, Wert, endgültig (in ini schreiben)
    std::function<void()> closed;                               // Fenster wurde geschlossen
};

class SettingsWindow {
public:
    explicit SettingsWindow(SettingsContext ctx) : ctx_(std::move(ctx)) {}
    ~SettingsWindow();
    void Show();                 // öffnet das Fenster oder holt es nach vorn
    HWND Hwnd() const { return hwnd_; }
    void Refresh(bool rescanLibrary = false);   // Zustand neu lesen (Häkchen, Auswahl, Eigenschaften)

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PageProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(HWND, UINT, WPARAM, LPARAM);

    int S(int v) const { return MulDiv(v, (int)dpi_, 96); }
    void MakeFont();
    void ApplyFont();
    void CreateControls();
    void Layout();
    void LayoutSettingsPage();
    void LayoutPropsPage();
    void RebuildList();
    void RebuildProps();
    void ShowTab(int i);
    void UpdateScroll();
    void ScrollTo(int pos);
    void MakeNextThumbnail();
    void ApplyItem(int index);
    void OnScroll(HWND bar, WPARAM wp);
    void OnPropColor(int index);
    void Close();
    std::wstring CurrentId() const;
    int Target() const;          // -1 = alle Bildschirme

    SettingsContext ctx_;
    HWND hwnd_ = nullptr, tab_ = nullptr, list_ = nullptr, screenLabel_ = nullptr, screenCombo_ = nullptr;
    HWND pageProps_ = nullptr, pageSettings_ = nullptr, propsTitle_ = nullptr;
    HWND fpsCombo_ = nullptr, scaleCombo_ = nullptr, fpsLabel_ = nullptr, scaleLabel_ = nullptr;
    std::vector<std::pair<UINT, HWND>> checks_, buttons_;   // Menübefehl -> Steuerelement
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    int thumbW_ = 0, thumbH_ = 0;

    std::vector<Wallpaper> items_;
    std::vector<SettingsScreen> screens_;
    size_t nextThumb_ = 0;
    std::wstring currentId_, propsKey_;
    std::vector<SettingsProp> props_;
    std::vector<HWND> propCtrls_;      // alles, was zu den Eigenschaften gehört (zum Aufräumen)
    std::vector<HWND> propValueLabels_, propSliders_, propSwatches_;
    int contentH_ = 0, scrollPos_ = 0;
    bool comboShown_ = false;          // Bildschirm-Auswahl sichtbar (nur mit mehreren Bildschirmen ohne Panorama)
};
