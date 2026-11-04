// Lumen – schlanke Live-Hintergründe für Windows
// Ein Prozess, ein D3D11-Gerät, kein Browser.

#include "common.h"
#include "config.h"
#include "desktop.h"
#include "audio.h"
#include "renderer.h"
#include "library.h"
#include "schedule.h"
#include "commands.h"
#include "settings.h"
#include <shellapi.h>
#include <commdlg.h>
#include <wtsapi32.h>
#include <dwmapi.h>
#include <thread>
#include <mutex>
#include <cmath>
#include <algorithm>

#define IDI_APPICON 1

enum : UINT {
    WM_TRAY = WM_APP + 1,
    WM_APP_CATALOG,
    WM_APP_INSTALLED,
    WM_APP_HELLO,
    WM_APP_SCREENPAINT,
    WM_APP_SETTINGS_CLOSED,
};

static const int kNone = 999;

static const UINT TIMER_CHECK = 1, TIMER_REBUILD = 2;
static const GUID kGUID_CONSOLE_DISPLAY_STATE = {0x6FE69556, 0x704A, 0x47A0, {0x8F, 0x24, 0xC2, 0x8D, 0x93, 0x6F, 0xDA, 0x47}};

struct Screen {
    MonitorInfo mon;
    HWND hwnd = nullptr;
    ComPtr<IDXGISwapChain1> sc;
    ComPtr<ID3D11RenderTargetView> rtv;
    int rw = 0, rh = 0;
    std::wstring wallId;
    Wallpaper wp;
    bool hasWall = false;
    std::shared_ptr<Source> src;
    std::shared_ptr<Program> prog;
    FILETIME shaderTime{}, iniTime{};
    std::vector<ShaderParam> params;   // Eigenschaften des Shaders (// @param ...)
    float pv[kMaxParams][4] = {};      // ihre aktuellen Werte
    bool covered = false, dirty = true, srcDropped = false;
    double lastRender = -1, startTime = 0;
    float clickX = 0, clickY = 0;
    double clickTime = -1;
    int clickCount = 0;

    bool Animated() const { return (prog && prog->usesTime) || (src && src->Animated()); }
};

static std::wstring MenuText(const std::wstring& s) {
    std::wstring r;
    for (wchar_t c : s) { r += c; if (c == L'&') r += L'&'; }
    return r;
}

static FILETIME LastWrite(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return {};
    return fa.ftLastWriteTime;
}

static std::wstring FirstLine(const std::wstring& s) {
    // Erste Zeile mit "error", sonst die erste Zeile überhaupt (Warnungen interessieren hier nicht)
    std::wstring l = s.substr(0, s.find_first_of(L"\r\n"));
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find_first_of(L"\r\n", pos);
        std::wstring line = s.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        if (ToLower(line).find(L"error") != std::wstring::npos) { l = line; break; }
        if (e == std::wstring::npos) break;
        pos = e + 1;
    }
    // "wallpaper.hlsl(12,5-9): error X3004: ..." -> kürzen
    // "wallpaper.hlsl(12,5-9): error X3004: ..." -> "Zeile 12: error X3004: ..."
    size_t p = l.find(L"wallpaper.hlsl(");
    if (p != std::wstring::npos) {
        size_t close = l.find(L"):", p);
        std::wstring pos = l.substr(p + 15, close == std::wstring::npos ? 0 : close - p - 15);
        l = L"Zeile " + pos.substr(0, pos.find(L',')) + (close == std::wstring::npos ? L"" : l.substr(close + 1));
    }
    if (l.size() > 200) l.resize(200);
    return l;
}

class App {
public:
    int Run(HINSTANCE inst, const std::wstring& cmdFile);

private:
    static LRESULT CALLBACK MsgProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ScreenProc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    // Bildschirme
    void BuildScreens();
    void DestroyScreens();
    bool CreateScreenWindow(Screen& s);
    void DestroyScreenWindow(Screen& s);
    void LoadWallpaper(Screen& s, const std::wstring& id);
    std::wstring AssignedId(const MonitorInfo& m) const;
    bool SpanActive() const { return screens_.size() > 1 && cfg_.GetBool(L"span", false); }
    RECT SpanBox() const;   // kleinstes Rechteck um alle Bildschirme (physische Pixel)
    void ApplyId(Screen& s, const std::wstring& id);
    void SetAll(const std::wstring& id);
    void SetScreen(size_t i, const std::wstring& id);
    std::shared_ptr<Source> GetSource(const std::wstring& path, bool video, std::wstring& err);
    void ResizeAll();
    void FitRenderSize(Screen& s);

    // Ablauf
    double RenderDue(double now);
    void RenderScreen(Screen& s, double now, const AudioFrame& af, POINT cur, bool over, bool down);
    void PeriodicCheck();
    void CheckCoverage();
    void UpdateAudioNeed();
    bool GlobalPaused() const;
    bool Visible(const Screen& s) const { return s.hwnd && s.hasWall && !s.covered && !GlobalPaused(); }
    void TrimMemory();
    void RecoverDevice();

    // Tray
    void AddTray();
    void Balloon(const std::wstring& title, const std::wstring& text);
    void ShowMenu();
    HMENU BuildWallMenu(UINT base, const std::wstring& current);
    void OnCommand(UINT id);
    void AddFileDialog();
    void AddFileAndApply(const std::wstring& path);
    void RefreshCatalog();
    void InstallCatalog(size_t idx);
    void OpenConfig();
    void OpenSettings();
    bool MenuChecked(UINT id) const;   // Zustand eines Schalters (Menü und Einstellungsfenster)
    void UpdateSchedule();   // wechselt den Hintergrund, wenn laut Zeitplan ein neuer Abschnitt beginnt
    void EditSchedule();
    const Screen* PropertyScreen() const;   // erster Bildschirm, dessen Hintergrund einstellbare Werte hat
    void EditProperties();
    void Quit();

    HINSTANCE inst_ = nullptr;
    HWND msgWnd_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    NOTIFYICONDATAW nid_{};
    HICON icon_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;

    Config cfg_;
    Library lib_;
    Gfx gfx_;
    AudioAnalyzer audio_;
    DesktopHost host_;
    std::vector<Screen> screens_;
    std::map<std::wstring, std::weak_ptr<Source>> sourceCache_;
    std::vector<Wallpaper> menuItems_;

    bool running_ = true;
    bool userPaused_ = false, locked_ = false, displayOff_ = false;
    bool onBattery_ = false, saver_ = false, remote_ = false, fullscreen_ = false;
    bool recheckSoon_ = false;
    bool prevDown_ = false;
    bool trimmed_ = false;
    double lastActive_ = 0, lastRecheck_ = 0;
    uint64_t frameId_ = 0;

    // Zeitplan (zeitplan.txt): scheduled_ ist der Hintergrund des aktuellen Abschnitts; wählt man selbst einen,
    // bleibt der bis zum nächsten Abschnitt (scheduleHeld_)
    std::unique_ptr<SettingsWindow> settings_;
    std::vector<ScheduleEntry> plan_;
    FILETIME planTime_{};
    std::wstring scheduled_, slotKey_;
    bool scheduleHeld_ = false;

    std::mutex catMtx_;
    std::vector<CatalogItem> catalog_;
    std::wstring catErr_, installRel_, installErr_;
    bool catBusy_ = false;

    static App* self_;
};
App* App::self_ = nullptr;

// =====================================================================================
//  Start
// =====================================================================================
int App::Run(HINSTANCE inst, const std::wstring& cmdFile) {
    self_ = this;
    inst_ = inst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LogInit();
    Log(L"Lumen %s startet", APP_VERSION);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    cfg_.Load();
    lib_.WriteExamples();
    lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));

    if (!gfx_.Init(true)) {
        MessageBoxW(nullptr, L"Direct3D 11 konnte nicht gestartet werden.\nLumen benötigt eine Grafikkarte mit Direct3D 11.",
                    APP_NAME, MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"LumenTray";
    RegisterClassExW(&wc);
    WNDCLASSEXW sc{sizeof(sc)};
    sc.lpfnWndProc = ScreenProc;
    sc.hInstance = inst;
    sc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    sc.lpszClassName = L"LumenScreen";
    RegisterClassExW(&sc);

    // Unsichtbares Hauptfenster (top-level, damit es Broadcasts wie TaskbarCreated bekommt)
    msgWnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"LumenTray", APP_NAME, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(msgWnd_, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);

    icon_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                              GetSystemMetrics(SM_CYSMICON), 0);
    if (!icon_) icon_ = LoadIcon(nullptr, IDI_APPLICATION);
    AddTray();

    WTSRegisterSessionNotification(msgWnd_, NOTIFY_FOR_THIS_SESSION);
    RegisterPowerSettingNotification(msgWnd_, &kGUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MINIMIZEEND, nullptr, WinEventProc, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    host_.Attach(cfg_.GetBool(L"desktop_layered", false));
    UpdateSchedule();   // vor den Bildschirmen, damit gleich der Hintergrund der aktuellen Tageszeit erscheint
    BuildScreens();
    SetTimer(msgWnd_, TIMER_CHECK, 1000, nullptr);
    PeriodicCheck();

    if (cfg_.Get(L"first_run_done").empty()) {
        cfg_.SetInt(L"first_run_done", 1);
        cfg_.Save();
        Balloon(L"Lumen läuft", L"Rechtsklick auf das Lumen-Symbol in der Taskleiste, um Hintergründe zu wählen.");
    }
    if (!cmdFile.empty()) AddFileAndApply(cmdFile);

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);

    while (running_) {
        double now = NowSeconds();
        if (recheckSoon_ && now - lastRecheck_ > 0.15) {
            recheckSoon_ = false;
            lastRecheck_ = now;
            CheckCoverage();
            UpdateAudioNeed();
        }
        if (gfx_.DeviceLost()) RecoverDevice();
        double next = RenderDue(now);

        DWORD count = 0;
        DWORD timeout = INFINITE;
        if (next >= 0) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(std::max(0.0, next - NowSeconds()) * 1e7);
            if (due.QuadPart == 0) due.QuadPart = -1;
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            count = 1;
        } else if (recheckSoon_) {
            timeout = 160;
        }
        MsgWaitForMultipleObjectsEx(count, &timer, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_ = false; break; }
            // Tab, Pfeiltasten und Esc im Einstellungsfenster
            if (settings_ && settings_->Hwnd() && IsDialogMessageW(settings_->Hwnd(), &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CloseHandle(timer);
    return 0;
}

// =====================================================================================
//  Fenster-Prozeduren
// =====================================================================================
LRESULT CALLBACK App::MsgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (self_ && self_->msgWnd_ == h) return self_->HandleMsg(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK App::ScreenProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        if (self_ && self_->msgWnd_) PostMessageW(self_->msgWnd_, WM_APP_SCREENPAINT, (WPARAM)h, 0);
        return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, m, w, l);
}

void CALLBACK App::WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (self_) self_->recheckSoon_ = true;
}

LRESULT App::HandleMsg(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == taskbarCreatedMsg_ && m != 0) {
        AddTray();
        recheckSoon_ = true;
        return 0;
    }
    switch (m) {
    case WM_TRAY:
        if (LOWORD(l) == WM_LBUTTONUP) OpenSettings();   // Linksklick: Fenster mit Vorschau, Rechtsklick: Menü
        else if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) ShowMenu();
        return 0;
    case WM_APP_SETTINGS_CLOSED:
        if (settings_ && !settings_->Hwnd()) {   // (war er inzwischen wieder geöffnet, bleibt er)
            settings_.reset();
            TrimMemory();
        }
        return 0;
    case WM_TIMER:
        if (w == TIMER_CHECK) PeriodicCheck();
        else if (w == TIMER_REBUILD) {
            KillTimer(h, TIMER_REBUILD);
            BuildScreens();
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        SetTimer(h, TIMER_REBUILD, 800, nullptr);
        return 0;
    case WM_SETTINGCHANGE:
        if (w == SPI_SETWORKAREA) {
            auto mons = EnumMonitors();
            for (auto& s : screens_)
                for (auto& mm : mons)
                    if (mm.device == s.mon.device) s.mon.work = mm.work;
            recheckSoon_ = true;
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (w == WTS_SESSION_LOCK) locked_ = true;
        if (w == WTS_SESSION_UNLOCK) { locked_ = false; for (auto& s : screens_) s.dirty = true; }
        UpdateAudioNeed();
        return 0;
    case WM_POWERBROADCAST:
        if (w == PBT_POWERSETTINGCHANGE) {
            auto* ps = reinterpret_cast<POWERBROADCAST_SETTING*>(l);
            if (ps && IsEqualGUID(ps->PowerSetting, kGUID_CONSOLE_DISPLAY_STATE) && ps->DataLength >= sizeof(DWORD)) {
                displayOff_ = *reinterpret_cast<DWORD*>(ps->Data) == 0;
                UpdateAudioNeed();
            }
        } else if (w == PBT_APMRESUMEAUTOMATIC) {
            recheckSoon_ = true;
            for (auto& s : screens_) s.dirty = true;
        }
        return TRUE;
    case WM_APP_SCREENPAINT:
        for (auto& s : screens_)
            if (s.hwnd == (HWND)w) s.dirty = true;
        return 0;
    case WM_APP_HELLO:
        Balloon(L"Lumen läuft bereits", L"Rechtsklick auf das Symbol in der Taskleiste öffnet das Menü.");
        return 0;
    case WM_COPYDATA: {
        auto* cd = reinterpret_cast<COPYDATASTRUCT*>(l);
        if (cd && cd->dwData == 0x4C554D && cd->cbData >= sizeof(wchar_t)) {
            std::wstring p((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t));
            while (!p.empty() && p.back() == 0) p.pop_back();
            AddFileAndApply(p);
        }
        return TRUE;
    }
    case WM_APP_CATALOG: {
        catBusy_ = false;
        std::lock_guard<std::mutex> lk(catMtx_);
        if (w) Balloon(L"Online-Bibliothek", std::to_wstring(catalog_.size()) + L" Hintergründe verfügbar. Menü → Online-Bibliothek.");
        else Balloon(L"Online-Bibliothek", catErr_);
        return 0;
    }
    case WM_APP_INSTALLED: {
        catBusy_ = false;
        std::wstring rel, err;
        {
            std::lock_guard<std::mutex> lk(catMtx_);
            rel = installRel_;
            err = installErr_;
        }
        if (!w) {
            Balloon(L"Download fehlgeschlagen", err);
            return 0;
        }
        lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
        for (auto& it : lib_.Items()) {
            std::wstring a = ToLower(it.id), b = ToLower(rel);
            if (a == b || a.rfind(b + L"\\", 0) == 0) {
                SetAll(it.id);
                Balloon(L"Installiert", it.name);
                return 0;
            }
        }
        Balloon(L"Installiert", L"Kein gültiger Hintergrund im Download gefunden.");
        return 0;
    }
    case WM_ENDSESSION:
        if (w) {
            DestroyScreens();
            Shell_NotifyIconW(NIM_DELETE, &nid_);
        }
        return 0;
    case WM_CLOSE:   // z. B. "taskkill /im Lumen.exe" ohne /F: sauber beenden
        Quit();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// =====================================================================================
//  Bildschirme
// =====================================================================================
RECT App::SpanBox() const {
    RECT box{LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    for (auto& s : screens_) {
        box.left = std::min(box.left, s.mon.rc.left);
        box.top = std::min(box.top, s.mon.rc.top);
        box.right = std::max(box.right, s.mon.rc.right);
        box.bottom = std::max(box.bottom, s.mon.rc.bottom);
    }
    return box;
}

std::wstring App::AssignedId(const MonitorInfo& m) const {
    if (!scheduled_.empty() && !scheduleHeld_) return scheduled_;
    // Im Panorama zeigen alle Bildschirme denselben Hintergrund; einzeln gewählte bleiben für später gespeichert
    std::wstring id = SpanActive() ? L"" : cfg_.Get(L"screen." + m.device);
    if (id.empty()) id = cfg_.Get(L"default");
    if (id.empty()) id = L"Beispiele\\Aurora";
    return id;
}

void App::BuildScreens() {
    DestroyScreens();
    if (!host_.IsValid()) host_.Attach(cfg_.GetBool(L"desktop_layered", false));
    for (auto& m : EnumMonitors()) {
        Screen s;
        s.mon = m;
        screens_.push_back(s);
    }
    for (auto& s : screens_) ApplyId(s, AssignedId(s.mon));
    Log(L"%d Bildschirm(e) eingerichtet", (int)screens_.size());
    recheckSoon_ = true;
}

void App::DestroyScreens() {
    for (auto& s : screens_) DestroyScreenWindow(s);
    screens_.clear();
    sourceCache_.clear();
    gfx_.Trim();
}

bool App::CreateScreenWindow(Screen& s) {
    const RECT& r = s.mon.rc;
    int w = r.right - r.left, h = r.bottom - r.top;
    s.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"LumenScreen", L"", WS_POPUP, r.left, r.top, w, h,
                             nullptr, nullptr, inst_, nullptr);
    if (!s.hwnd) return false;
    host_.Place(s.hwnd, r);
    s.rw = s.rh = 0;
    FitRenderSize(s);   // noch ohne Swapchain: legt nur die Größe fest
    if (!gfx_.CreateSwapChain(s.hwnd, s.rw, s.rh, cfg_.GetBool(L"legacy_swapchain", false), s.sc, s.rtv)) {
        DestroyWindow(s.hwnd);
        s.hwnd = nullptr;
        return false;
    }
    s.dirty = true;
    return true;
}

void App::DestroyScreenWindow(Screen& s) {
    s.src.reset();
    s.prog.reset();
    s.rtv.Reset();
    if (gfx_.Ctx()) {
        gfx_.Ctx()->OMSetRenderTargets(0, nullptr, nullptr);
        gfx_.Ctx()->Flush();
    }
    s.sc.Reset();
    if (s.hwnd && IsWindow(s.hwnd)) DestroyWindow(s.hwnd);
    s.hwnd = nullptr;
    s.hasWall = false;
}

void App::ResizeAll() {
    for (auto& s : screens_)
        if (s.sc) FitRenderSize(s);
}

void App::FitRenderSize(Screen& s) {
    int w = s.mon.rc.right - s.mon.rc.left, h = s.mon.rc.bottom - s.mon.rc.top;
    double scale = cfg_.RenderScale();
    // Reine Shader sind weich gezeichnet: Über ~1080p sieht man kaum Unterschied, die GPU-Last wächst aber mit jedem Pixel
    int maxH = cfg_.GetInt(L"shader_res", 1080);
    if (s.hasWall && s.wp.kind == WallKind::Shader && maxH > 0)
        scale = std::min(scale, std::sqrt(maxH * maxH * 16.0 / 9.0 / (double(w) * h)));
    int rw = std::max(1, (int)std::lround(w * scale)), rh = std::max(1, (int)std::lround(h * scale));
    if (rw == s.rw && rh == s.rh) return;
    s.rw = rw;
    s.rh = rh;
    if (s.sc) gfx_.ResizeSwapChain(s.sc.Get(), rw, rh, s.rtv);
    s.dirty = true;
}

std::shared_ptr<Source> App::GetSource(const std::wstring& path, bool video, std::wstring& err) {
    std::wstring key = ToLower(path);
    auto it = sourceCache_.find(key);
    if (it != sourceCache_.end())
        if (auto s = it->second.lock()) return s;
    auto s = video ? OpenVideoSource(gfx_, path, err) : LoadImageSource(gfx_, path, err);
    if (s) sourceCache_[key] = s;
    return s;
}

void App::LoadWallpaper(Screen& s, const std::wstring& id) {
    s.src.reset();
    s.prog.reset();
    s.srcDropped = false;
    s.wallId = id;
    s.dirty = true;
    s.startTime = NowSeconds();
    const Wallpaper* w = lib_.Find(id);
    if (!w && !lib_.Items().empty()) {
        Log(L"Hintergrund '%s' nicht gefunden, nehme ersten der Bibliothek", id.c_str());
        w = &lib_.Items()[0];
    }
    if (!w) {
        s.hasWall = false;
        return;
    }
    s.wp = *w;
    s.hasWall = true;
    s.params.clear();
    memset(s.pv, 0, sizeof(s.pv));
    if (!w->iniPath.empty()) s.iniTime = LastWrite(w->iniPath);

    std::wstring err;
    if (w->kind == WallKind::Image || w->kind == WallKind::Video) {
        s.src = GetSource(w->source, w->kind == WallKind::Video, err);
        if (s.src) s.src->SetSpeed(w->speed);
        else {
            Balloon(L"Hintergrund konnte nicht geladen werden", err);
            s.prog = gfx_.ErrorProgram();
            return;
        }
    }
    std::string code;
    if (!w->shaderPath.empty()) {
        if (!ReadTextFile(w->shaderPath, code)) {
            Balloon(L"Shader nicht lesbar", FileNameOf(w->shaderPath));
            s.prog = gfx_.ErrorProgram();
            return;
        }
        s.shaderTime = LastWrite(w->shaderPath);
        s.params = ParseShaderParams(code);
        for (size_t i = 0; i < s.params.size(); i++) ParamValue(s.params[i], w->ini, s.pv[i]);
    } else {
        code = DefaultMediaShader();
    }
    std::wstring perr;
    s.prog = gfx_.GetProgram(code, w->fit, perr);
    if (!s.prog) {
        Log(L"Shader-Fehler in %s:\n%s", w->name.c_str(), perr.c_str());
        Balloon(L"Shader-Fehler: " + w->name, FirstLine(perr) + L"\n(Details in lumen.log)");
        s.prog = gfx_.ErrorProgram();
    }
}

void App::ApplyId(Screen& s, const std::wstring& id) {
    if (id == L"none") {
        if (s.hwnd) {
            DestroyScreenWindow(s);
            // Explorer zeichnet dort wieder den normalen Hintergrund
            if (HWND p = host_.Parent()) RedrawWindow(p, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            else RefreshSystemWallpaper();
        }
        s.wallId = id;
        return;
    }
    if (!s.hwnd && !CreateScreenWindow(s)) {
        Balloon(L"Fehler", L"Fenster hinter dem Desktop konnte nicht erstellt werden.");
        return;
    }
    LoadWallpaper(s, id);
    FitRenderSize(s);
    UpdateAudioNeed();
}

void App::SetAll(const std::wstring& id) {
    scheduleHeld_ = true;   // eigene Wahl gilt bis zum nächsten Abschnitt des Zeitplans
    cfg_.Set(L"default", id);
    for (auto& s : screens_) cfg_.Remove(L"screen." + s.mon.device);
    cfg_.Save();
    for (auto& s : screens_) ApplyId(s, id);
    TrimMemory();
}

void App::SetScreen(size_t i, const std::wstring& id) {
    if (i >= screens_.size()) return;
    scheduleHeld_ = true;
    cfg_.Set(L"screen." + screens_[i].mon.device, id);
    cfg_.Save();
    ApplyId(screens_[i], id);
    TrimMemory();
}

void App::RecoverDevice() {
    Log(L"Grafikgerät verloren – starte neu");
    DestroyScreens();
    gfx_.Shutdown();
    if (!gfx_.Init(true)) {
        Balloon(L"Grafikfehler", L"Direct3D konnte nicht neu gestartet werden.");
        running_ = false;
        return;
    }
    BuildScreens();
}

// =====================================================================================
//  Zeichnen
// =====================================================================================
bool App::GlobalPaused() const {
    bool battery = cfg_.GetBool(L"pause_on_battery", true) && (onBattery_ || saver_);
    bool remote = cfg_.GetBool(L"pause_remote", true) && remote_;
    return userPaused_ || locked_ || displayOff_ || battery || remote || fullscreen_;
}

double App::RenderDue(double now) {
    if (screens_.empty() || GlobalPaused()) return -1;

    // Eingaben einmal pro Durchlauf
    POINT cur{};
    GetCursorPos(&cur);
    bool over = IsPointOverDesktop(cur);
    int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    SHORT ks = GetAsyncKeyState(vk);
    bool down = (ks & 0x8000) != 0;
    // Bit 0: "seit letzter Abfrage gedrückt" – fängt auch Klicks zwischen zwei Bildern
    bool pressed = ((down && !prevDown_) || (ks & 1)) && over;
    prevDown_ = down;
    if (pressed) {
        RECT box = SpanBox();
        bool span = SpanActive();
        for (auto& s : screens_) {
            if (span) {
                // Ein Klick gilt für das ganze Panorama, damit z. B. die Welle über Bildschirmränder weiterläuft
                s.clickX = float(cur.x - box.left);
                s.clickY = float(cur.y - box.top);
            } else {
                if (!PtInRect(&s.mon.rc, cur)) continue;
                double sc = s.rw / double(std::max(1L, s.mon.rc.right - s.mon.rc.left));
                s.clickX = float((cur.x - s.mon.rc.left) * sc);
                s.clickY = float((cur.y - s.mon.rc.top) * sc);
            }
            s.clickTime = now;
            s.clickCount++;
        }
    }

    // Was ist fällig?
    std::vector<Screen*> due;
    double next = -1;
    for (auto& s : screens_) {
        if (!Visible(s)) continue;
        if (s.srcDropped && s.dirty) LoadWallpaper(s, s.wallId);   // freigegebenes Standbild neu laden
        if (!s.dirty && !s.Animated()) continue;
        int fps = cfg_.Fps();
        if (s.wp.fps > 0) fps = std::min(fps, s.wp.fps);
        double t = s.dirty ? now : s.lastRender + 1.0 / fps;
        // Quelle mit eigenem Takt (GIF): erst zeichnen, wenn ein neues Bild fällig ist, außer der Shader bewegt sich selbst
        if (!s.dirty && s.src && !(s.prog && s.prog->usesTime)) {
            double change = s.src->NextChange();
            if (change > 0) t = std::max(t, change);
        }
        if (t <= now + 0.001) {
            due.push_back(&s);
            t = now + 1.0 / fps;
        }
        if (s.Animated() && (next < 0 || t < next)) next = t;
    }
    if (due.empty()) return next;

    frameId_++;
    lastActive_ = now;
    trimmed_ = false;
    // Geteilte Quellen (Videos) genau einmal weiterschalten
    for (auto* s : due) {
        if (s->src && s->src->lastFrameId != frameId_) {
            s->src->lastFrameId = frameId_;
            s->src->Update(now, true);
        }
    }
    AudioFrame af = audio_.Running() ? audio_.Get() : AudioFrame{};
    for (auto* s : due) RenderScreen(*s, now, af, cur, over, down);
    return next;
}

void App::RenderScreen(Screen& s, double now, const AudioFrame& af, POINT cur, bool over, bool down) {
    if (!s.rtv) return;
    ShaderConstants c{};
    bool span = SpanActive();
    // Panorama: alle Bildschirme teilen eine Startzeit, sonst laufen die Animationen an den Rändern auseinander
    double start = span ? screens_[0].startTime : s.startTime;
    // float ist bei 86400 s nur noch ~8 ms genau (Animation ruckelt), bei 3600 s ~0,24 ms – dafür ein Sprung pro Stunde
    c.time = (float)std::fmod(now - start, 3600.0);
    c.timeDelta = s.lastRender < 0 ? 0.0f : (float)std::min(now - s.lastRender, 1.0);
    if (span) {
        // Shader sehen den ganzen Desktop in physischen Pixeln; dieser Bildschirm liegt an seiner Stelle darin
        RECT box = SpanBox();
        c.resolution[0] = (float)(box.right - box.left);
        c.resolution[1] = (float)(box.bottom - box.top);
        c.map[0] = (float)(s.mon.rc.left - box.left);
        c.map[1] = (float)(s.mon.rc.top - box.top);
        c.map[2] = (float)(s.mon.rc.right - s.mon.rc.left) / s.rw;
        c.map[3] = (float)(s.mon.rc.bottom - s.mon.rc.top) / s.rh;
        c.mouse[0] = (float)(cur.x - box.left);
        c.mouse[1] = (float)(cur.y - box.top);
        c.mouse[2] = (over && down) ? 1.0f : 0.0f;
        c.mouse[3] = over ? 1.0f : 0.0f;
    } else {
        c.resolution[0] = (float)s.rw;
        c.resolution[1] = (float)s.rh;
        double sc = s.rw / double(std::max(1L, s.mon.rc.right - s.mon.rc.left));
        bool onThis = PtInRect(&s.mon.rc, cur) != 0;
        c.mouse[0] = float((cur.x - s.mon.rc.left) * sc);
        c.mouse[1] = float((cur.y - s.mon.rc.top) * sc);
        c.mouse[2] = (onThis && over && down) ? 1.0f : 0.0f;
        c.mouse[3] = (onThis && over) ? 1.0f : 0.0f;
    }
    c.audio[0] = af.bass;
    c.audio[1] = af.mid;
    c.audio[2] = af.treble;
    c.audio[3] = af.level;
    for (int i = 0; i < 16; i++) c.bands[i] = af.bands[i];
    c.click[0] = s.clickX;
    c.click[1] = s.clickY;
    c.click[2] = s.clickTime < 0 ? 10000.0f : (float)(now - s.clickTime);
    c.click[3] = (float)s.clickCount;
    if (s.src) {
        c.channelRes[0] = (float)s.src->Width();
        c.channelRes[1] = (float)s.src->Height();
    }
    SYSTEMTIME lt;
    GetLocalTime(&lt);
    double hourOverride = cfg_.GetFloat(L"debug_hour", -1);
    c.hour = hourOverride >= 0 ? (float)hourOverride : lt.wHour + lt.wMinute / 60.0f + lt.wSecond / 3600.0f;
    c.frame = (float)(frameId_ % 1000000);
    memcpy(c.params, s.pv, sizeof(c.params));

    gfx_.Draw(s.rtv.Get(), s.rw, s.rh, s.prog.get(), s.src.get(), c);
    HRESULT hr = s.sc->Present(0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) gfx_.MarkLost();
    s.dirty = false;
    s.lastRender = now;
}

// =====================================================================================
//  Regelmäßige Prüfungen (1×/Sekunde)
// =====================================================================================
void App::CheckCoverage() {
    bool fs = cfg_.GetBool(L"pause_fullscreen", true) && IsForegroundFullscreen();
    if (fs != fullscreen_) {
        fullscreen_ = fs;
        if (!fs) for (auto& s : screens_) s.dirty = true;
    }
    for (auto& s : screens_) {
        if (!s.hwnd) continue;
        bool cov = IsMonitorCovered(s.mon);
        if (cov != s.covered) {
            s.covered = cov;
            if (!cov) s.dirty = true;
        }
    }
}

void App::UpdateAudioNeed() {
    bool need = false;
    if (cfg_.GetBool(L"audio", true))
        for (auto& s : screens_)
            if (Visible(s) && s.prog && s.prog->usesAudio) need = true;
    if (need && !audio_.Running()) audio_.Start();
    if (!need && audio_.Running()) audio_.Stop();
}

void App::PeriodicCheck() {
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        onBattery_ = ps.ACLineStatus == 0;
        saver_ = ps.SystemStatusFlag == 1;
    }
    remote_ = GetSystemMetrics(SM_REMOTESESSION) != 0;

    // Explorer neu gestartet oder Desktop umgebaut?
    bool broken = !host_.IsValid() || host_.ExplorerAppeared();
    for (auto& s : screens_)
        if (s.hwnd && !IsWindow(s.hwnd)) broken = true;
    if (broken) {
        Log(L"Desktop-Fenster verloren – richte neu ein");
        host_.Attach(cfg_.GetBool(L"desktop_layered", false));
        BuildScreens();
    }

    UpdateSchedule();
    CheckCoverage();

    // Shader oder wallpaper.ini live neu laden, wenn die Datei gespeichert wurde
    for (auto& s : screens_) {
        if (!s.hasWall) continue;
        bool changed = false;
        if (!s.wp.shaderPath.empty()) {
            FILETIME t = LastWrite(s.wp.shaderPath);
            changed = CompareFileTime(&t, &s.shaderTime) != 0;
        }
        if (!s.wp.iniPath.empty()) {
            FILETIME t = LastWrite(s.wp.iniPath);
            changed = changed || CompareFileTime(&t, &s.iniTime) != 0;
        }
        if (changed) {
            Log(L"Geändert, lade neu: %s", s.wp.id.c_str());
            lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));   // neue Werte aus der ini übernehmen
            LoadWallpaper(s, s.wallId);
        }
    }
    UpdateAudioNeed();

    // Nichts bewegt sich seit 30 s → Speicher freigeben
    double now = NowSeconds();
    bool anyAnimating = false;
    for (auto& s : screens_)
        if (Visible(s) && s.Animated()) anyAnimating = true;
    if (!anyAnimating && !trimmed_ && now - lastActive_ > 30) {
        for (auto& s : screens_) {
            // Standbilder: die Textur wird nicht mehr gebraucht, das Bild liegt schon im Fenster
            if (s.src && !s.Animated() && !s.dirty) {
                s.src.reset();
                s.srcDropped = true;
            }
        }
        TrimMemory();
        trimmed_ = true;
    }
}

void App::TrimMemory() {
    for (auto it = sourceCache_.begin(); it != sourceCache_.end();) {
        if (it->second.expired()) it = sourceCache_.erase(it);
        else ++it;
    }
    gfx_.Trim();
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// =====================================================================================
//  Tray
// =====================================================================================
void App::AddTray() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = msgWnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAY;
    nid_.hIcon = icon_;
    wcscpy_s(nid_.szTip, L"Lumen – Live-Hintergründe");
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void App::Balloon(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = nid_;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO | 0x10 /*NIIF_NOSOUND*/;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

HMENU App::BuildWallMenu(UINT base, const std::wstring& current) {
    HMENU m = CreatePopupMenu();
    std::map<std::wstring, HMENU> groups;
    std::vector<std::wstring> order;
    for (size_t i = 0; i < menuItems_.size() && i < 900; i++) {
        auto& w = menuItems_[i];
        UINT flags = MF_STRING | (ToLower(w.id) == ToLower(current) ? MF_CHECKED : 0);
        std::wstring label = MenuText(w.name);
        if (w.kind == WallKind::Video) label += L"\t▶";
        else if (w.kind == WallKind::Image) label += L"\t▣";
        if (w.group.empty()) {
            AppendMenuW(m, flags, base + (UINT)i, label.c_str());
        } else {
            if (!groups.count(w.group)) {
                groups[w.group] = CreatePopupMenu();
                order.push_back(w.group);
            }
            AppendMenuW(groups[w.group], flags, base + (UINT)i, label.c_str());
        }
    }
    if (!order.empty() && GetMenuItemCount(m) > 0) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    for (auto& g : order) AppendMenuW(m, MF_POPUP, (UINT_PTR)groups[g], MenuText(g).c_str());
    if (menuItems_.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"(Bibliothek ist leer)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (current == L"none" ? MF_CHECKED : 0), base + kNone, L"Kein Live-Hintergrund");
    return m;
}

void App::ShowMenu() {
    lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
    menuItems_ = lib_.Items();

    HMENU m = CreatePopupMenu();
    std::wstring common = screens_.empty() ? L"" : screens_[0].wallId;
    for (auto& s : screens_)
        if (s.wallId != common) common.clear();

    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"Hintergründe und Einstellungen…");
    SetMenuDefaultItem(m, ID_SETTINGS, FALSE);   // fett: das macht auch ein Linksklick auf das Symbol
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    if (SpanActive()) {
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildWallMenu(ID_ALL_BASE, common), L"Hintergrund (Panorama über alle Bildschirme)");
    } else if (screens_.size() > 1) {
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildWallMenu(ID_ALL_BASE, common), L"Hintergrund für alle Bildschirme");
        for (size_t i = 0; i < screens_.size() && i < 16; i++) {
            auto& s = screens_[i];
            wchar_t label[96];
            swprintf(label, 96, L"Bildschirm %d (%dx%d)%s", (int)i + 1, (int)(s.mon.rc.right - s.mon.rc.left),
                     (int)(s.mon.rc.bottom - s.mon.rc.top), s.mon.primary ? L" – Haupt" : L"");
            AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildWallMenu(ID_SCREEN_BASE + (UINT)i * 1000, s.wallId), label);
        }
    } else {
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildWallMenu(ID_ALL_BASE, common), L"Hintergrund");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_ADDFILE, L"Bild oder Video hinzufügen…");
    AppendMenuW(m, MF_STRING, ID_NEWSHADER, L"Eigenen Shader erstellen…");
    if (const Screen* ps = PropertyScreen())
        AppendMenuW(m, MF_STRING, ID_PROPS, (L"Eigenschaften von „" + MenuText(ps->wp.name) + L"“ ändern…").c_str());
    AppendMenuW(m, MF_STRING, ID_OPENLIB, L"Bibliothek öffnen");

    HMENU cat = CreatePopupMenu();
    AppendMenuW(cat, MF_STRING | (catBusy_ ? MF_GRAYED : 0), ID_CAT_REFRESH, L"Katalog laden");
    AppendMenuW(cat, MF_STRING, ID_OPENCONFIG, L"Katalog-Adresse festlegen…");
    AppendMenuW(cat, MF_SEPARATOR, 0, nullptr);
    {
        std::lock_guard<std::mutex> lk(catMtx_);
        if (catalog_.empty())
            AppendMenuW(cat, MF_STRING | MF_GRAYED, 0,
                        cfg_.Get(L"catalog_url").empty() ? L"(keine Katalog-Adresse eingetragen)" : L"(noch nicht geladen)");
        for (size_t i = 0; i < catalog_.size() && i < 500; i++)
            AppendMenuW(cat, MF_STRING | (catBusy_ ? MF_GRAYED : 0), ID_CAT_BASE + (UINT)i, MenuText(catalog_[i].name).c_str());
    }
    AppendMenuW(m, MF_POPUP, (UINT_PTR)cat, L"Online-Bibliothek");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    HMENU set = CreatePopupMenu();
    HMENU fps = CreatePopupMenu();
    for (int f : {15, 30, 60, 144})
        AppendMenuW(fps, MF_STRING | (cfg_.Fps() == f ? MF_CHECKED : 0), ID_FPS_BASE + f, (std::to_wstring(f) + L" Bilder/s").c_str());
    AppendMenuW(set, MF_POPUP, (UINT_PTR)fps, L"Bildrate");
    HMENU scl = CreatePopupMenu();
    int curScale = (int)std::lround(cfg_.RenderScale() * 100);
    for (int p : {100, 75, 50})
        AppendMenuW(scl, MF_STRING | (curScale == p ? MF_CHECKED : 0), ID_SCALE_BASE + p, (std::to_wstring(p) + L" %").c_str());
    auto check = [&](HMENU menu, UINT id, const wchar_t* text) {
        AppendMenuW(menu, MF_STRING | (MenuChecked(id) ? MF_CHECKED : 0), id, text);
    };
    AppendMenuW(scl, MF_SEPARATOR, 0, nullptr);
    check(scl, ID_SHADER_RES, L"Shader höchstens in 1080p berechnen (spart GPU)");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)scl, L"Renderauflösung");
    check(set, ID_BATTERY, L"Im Akkubetrieb und Energiesparmodus pausieren");
    check(set, ID_FULLSCREEN, L"Bei Vollbild-Programmen alle Bildschirme pausieren");
    if (screens_.size() > 1 || MenuChecked(ID_SPAN)) check(set, ID_SPAN, L"Ein Hintergrund über alle Bildschirme (Panorama)");
    check(set, ID_SCHEDULE, L"Zeitplan: Hintergrund nach Tageszeit");
    AppendMenuW(set, MF_STRING, ID_EDITSCHEDULE, L"Zeitplan bearbeiten…");
    check(set, ID_AUDIO, L"Auf Musik reagieren");
    check(set, ID_WE, L"Videos aus Wallpaper Engine anzeigen");
    check(set, ID_AUTOSTART, L"Mit Windows starten");
    AppendMenuW(set, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(set, MF_STRING, ID_OPENCONFIG, L"Einstellungsdatei öffnen");
    AppendMenuW(set, MF_STRING, ID_RELOAD, L"Neu laden");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)set, L"Einstellungen");
    check(m, ID_PAUSE, L"Pausieren");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"Über Lumen");
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Beenden");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(msgWnd_);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, msgWnd_, nullptr);
    PostMessageW(msgWnd_, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) OnCommand(cmd);
}

void App::OnCommand(UINT id) {
    auto itemId = [&](UINT idx) -> std::wstring {
        if (idx == kNone) return L"none";
        return idx < menuItems_.size() ? menuItems_[idx].id : L"";
    };
    if (id >= ID_SCREEN_BASE && id < ID_SCREEN_BASE + 16 * 1000) {
        std::wstring w = itemId((id - ID_SCREEN_BASE) % 1000);
        if (!w.empty()) SetScreen((id - ID_SCREEN_BASE) / 1000, w);
        return;
    }
    if (id >= ID_ALL_BASE && id <= ID_ALL_BASE + kNone) {
        std::wstring w = itemId(id - ID_ALL_BASE);
        if (!w.empty()) SetAll(w);
        return;
    }
    if (id >= ID_CAT_BASE && id < ID_CAT_BASE + 500) { InstallCatalog(id - ID_CAT_BASE); return; }
    if (id >= ID_FPS_BASE && id < ID_FPS_BASE + 300) {
        cfg_.SetInt(L"fps", id - ID_FPS_BASE);
        cfg_.Save();
        return;
    }
    if (id >= ID_SCALE_BASE && id <= ID_SCALE_BASE + 100) {
        cfg_.Set(L"render_scale", std::to_wstring((id - ID_SCALE_BASE) / 100.0).substr(0, 4));
        cfg_.Save();
        ResizeAll();
        return;
    }
    switch (id) {
    case ID_PAUSE:
        userPaused_ = !userPaused_;
        for (auto& s : screens_) s.dirty = true;
        UpdateAudioNeed();
        break;
    case ID_ADDFILE: AddFileDialog(); break;
    case ID_SETTINGS: OpenSettings(); break;
    case ID_PROPS: EditProperties(); break;
    case ID_EDITSCHEDULE: EditSchedule(); break;
    case ID_SCHEDULE:
        cfg_.SetBool(L"schedule", !cfg_.GetBool(L"schedule", false));
        cfg_.Save();
        slotKey_.clear();
        scheduled_.clear();
        scheduleHeld_ = false;
        plan_.clear();
        planTime_ = {};   // Datei neu lesen
        if (cfg_.GetBool(L"schedule", false)) {
            UpdateSchedule();
            if (scheduled_.empty()) Balloon(L"Zeitplan", L"Noch keine Einträge: Menü → Einstellungen → Zeitplan bearbeiten.");
        } else {
            BuildScreens();   // zurück zum gewählten Hintergrund
        }
        break;
    case ID_NEWSHADER: {
        std::wstring nid, file;
        if (lib_.CreateShaderTemplate(nid, file)) {
            lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
            SetAll(nid);
            ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            Balloon(L"Shader erstellt", L"Bearbeite die Datei und speichere – Lumen lädt sie sofort neu.");
        }
        break;
    }
    case ID_OPENLIB: ShellExecuteW(nullptr, L"open", LibraryDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_CAT_REFRESH: RefreshCatalog(); break;
    case ID_OPENCONFIG: OpenConfig(); break;
    case ID_BATTERY: cfg_.SetBool(L"pause_on_battery", !cfg_.GetBool(L"pause_on_battery", true)); cfg_.Save(); break;
    case ID_SHADER_RES:
        cfg_.SetInt(L"shader_res", cfg_.GetInt(L"shader_res", 1080) > 0 ? 0 : 1080);
        cfg_.Save();
        ResizeAll();
        break;
    case ID_SPAN:
        cfg_.SetBool(L"span", !cfg_.GetBool(L"span", false));
        cfg_.Save();
        BuildScreens();   // lädt die Hintergründe nach der neuen Zuordnung neu
        break;
    case ID_FULLSCREEN:
        cfg_.SetBool(L"pause_fullscreen", !cfg_.GetBool(L"pause_fullscreen", true));
        cfg_.Save();
        recheckSoon_ = true;
        break;
    case ID_AUDIO: cfg_.SetBool(L"audio", !cfg_.GetBool(L"audio", true)); cfg_.Save(); UpdateAudioNeed(); break;
    case ID_WE: cfg_.SetBool(L"wallpaper_engine", !cfg_.GetBool(L"wallpaper_engine", true)); cfg_.Save(); break;
    case ID_AUTOSTART: SetAutostart(!AutostartEnabled()); break;
    case ID_RELOAD:
        cfg_.Load();
        lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
        BuildScreens();
        break;
    case ID_ABOUT: {
        std::wstring text = std::wstring(L"Lumen ") + APP_VERSION +
                            L"\n\nSchlanke Live-Hintergründe: Shader, Bilder und Videos hinter den Desktop-Symbolen – "
                            L"ohne Browser.\n\nBibliothek:\n" + LibraryDir() + L"\n\nEinstellungen und Log:\n" + AppDataDir();
        MessageBoxW(nullptr, text.c_str(), APP_NAME, MB_ICONINFORMATION);
        break;
    }
    case ID_EXIT: Quit(); break;
    }
}

void App::AddFileDialog() {
    static const wchar_t filter[] =
        L"Bilder, Videos und Shader\0*.jpg;*.jpeg;*.png;*.bmp;*.webp;*.gif;*.tif;*.tiff;*.mp4;*.webm;*.mkv;*.mov;*.wmv;*.avi;*.m4v;*.hlsl\0"
        L"Alle Dateien\0*.*\0\0";
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = msgWnd_;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrTitle = L"Hintergrund hinzufügen";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    SetForegroundWindow(msgWnd_);
    if (GetOpenFileNameW(&ofn)) AddFileAndApply(file);
}

void App::AddFileAndApply(const std::wstring& path) {
    std::wstring nid, err;
    if (!lib_.AddFile(path, nid, err)) {
        Balloon(L"Nicht hinzugefügt", err);
        return;
    }
    lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
    SetAll(nid);
}

bool App::MenuChecked(UINT id) const {
    switch (id) {
    case ID_BATTERY: return cfg_.GetBool(L"pause_on_battery", true);
    case ID_FULLSCREEN: return cfg_.GetBool(L"pause_fullscreen", true);
    case ID_AUDIO: return cfg_.GetBool(L"audio", true);
    case ID_SHADER_RES: return cfg_.GetInt(L"shader_res", 1080) > 0;
    case ID_SPAN: return cfg_.GetBool(L"span", false);
    case ID_SCHEDULE: return cfg_.GetBool(L"schedule", false);
    case ID_WE: return cfg_.GetBool(L"wallpaper_engine", true);
    case ID_AUTOSTART: return AutostartEnabled();
    case ID_PAUSE: return userPaused_;
    }
    return false;
}

void App::OpenSettings() {
    if (!settings_) {
        SettingsContext c;
        c.cfg = &cfg_;
        c.gfx = &gfx_;
        c.command = [this](UINT id) { OnCommand(id); };
        c.checked = [this](UINT id) { return MenuChecked(id); };
        c.items = [this] {
            lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
            return lib_.Items();
        };
        c.screens = [this] {
            std::vector<SettingsScreen> out;
            for (size_t i = 0; i < screens_.size(); i++) {
                auto& s = screens_[i];
                wchar_t label[96];
                swprintf(label, 96, L"Bildschirm %d (%dx%d)%s", (int)i + 1, (int)(s.mon.rc.right - s.mon.rc.left),
                         (int)(s.mon.rc.bottom - s.mon.rc.top), s.mon.primary ? L", Haupt" : L"");
                out.push_back({label, s.wallId});
            }
            return out;
        };
        c.select = [this](int screen, const std::wstring& id) {
            if (screen < 0) SetAll(id);
            else SetScreen((size_t)screen, id);
        };
        c.props = [this](std::wstring& name) {
            std::vector<SettingsProp> out;
            const Screen* ps = PropertyScreen();
            if (!ps) {
                for (auto& s : screens_)
                    if (s.hasWall) { name = s.wp.name; break; }
                return out;
            }
            name = ps->wp.name;
            for (size_t i = 0; i < ps->params.size(); i++) {
                SettingsProp p;
                p.p = ps->params[i];
                memcpy(p.v, ps->pv[i], sizeof(p.v));
                out.push_back(p);
            }
            return out;
        };
        c.setProp = [this](const std::wstring& name, const std::wstring& value, bool final) {
            const Screen* ps = PropertyScreen();
            if (!ps) return;
            std::wstring wallId = ps->wallId, ini = ps->wp.iniPath, key = L"param." + ToLower(name);
            size_t idx = ps->params.size();
            for (size_t i = 0; i < ps->params.size(); i++)
                if (ps->params[i].name == name) idx = i;
            if (idx == ps->params.size()) return;
            float v[4];
            ParamValue(ps->params[idx], {{key, value}}, v);
            // Sofort anzeigen, ohne den Hintergrund neu zu laden (das würde die Animation neu starten)
            for (auto& s : screens_)
                if (s.hasWall && s.wallId == wallId && idx < s.params.size()) {
                    memcpy(s.pv[idx], v, sizeof(v));
                    s.wp.ini[key] = value;
                    s.dirty = true;
                }
            if (!final) return;
            SetIniValue(ini, L"param." + name, value);
            lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
            FILETIME t = LastWrite(ini);   // damit die Dateiüberwachung nicht noch einmal neu lädt
            for (auto& s : screens_)
                if (s.hasWall && s.wallId == wallId) s.iniTime = t;
        };
        c.closed = [this] { PostMessageW(msgWnd_, WM_APP_SETTINGS_CLOSED, 0, 0); };
        settings_ = std::make_unique<SettingsWindow>(std::move(c));
    }
    settings_->Show();
}

static std::wstring SchedulePath() { return JoinPath(AppDataDir(), L"zeitplan.txt"); }

void App::UpdateSchedule() {
    if (!cfg_.GetBool(L"schedule", false)) return;
    FILETIME t = LastWrite(SchedulePath());
    if (CompareFileTime(&t, &planTime_) != 0) {
        planTime_ = t;
        std::string text;
        ReadTextFile(SchedulePath(), text);
        plan_ = ParseSchedule(text);
        Log(L"Zeitplan: %d Einträge", (int)plan_.size());
    }
    std::vector<ScheduleEntry> valid;   // unbekannte Hintergründe (umbenannt, gelöscht) überspringen
    for (auto& e : plan_)
        if (e.id == L"none" || lib_.Find(e.id)) valid.push_back(e);

    SYSTEMTIME lt;
    GetLocalTime(&lt);
    double debugHour = cfg_.GetFloat(L"debug_hour", -1);   // wie für iHour, zum Testen
    int minute = debugHour >= 0 ? (int)(debugHour * 60) % 1440 : lt.wHour * 60 + lt.wMinute;
    const ScheduleEntry* e = ActiveScheduleEntry(valid, minute);
    if (!e) return;   // leer: alles bleibt, wie es ist

    std::wstring key = std::to_wstring(e->minute) + L"|" + e->id;
    if (key == slotKey_) return;   // gleicher Abschnitt: eigene Wahl bleibt bestehen
    slotKey_ = key;
    scheduled_ = e->id;
    scheduleHeld_ = false;
    Log(L"Zeitplan: ab %02d:%02d %s", e->minute / 60, e->minute % 60, e->id.c_str());
    for (auto& s : screens_) ApplyId(s, e->id);
    TrimMemory();
}

void App::EditSchedule() {
    std::wstring path = SchedulePath();
    if (!FileExists(path)) {
        lib_.Scan(cfg_.GetBool(L"wallpaper_engine", true));
        std::wstring t =
            L"# Zeitplan: Lumen wechselt den Hintergrund nach der Uhrzeit.\r\n"
            L"# Pro Wechsel eine Zeile:   Uhrzeit | Hintergrund\r\n"
            L"# Der Hintergrund gilt ab dieser Uhrzeit bis zur nächsten Zeile, auch über Mitternacht hinweg.\r\n"
            L"# Wählst du selbst einen Hintergrund, bleibt er bis zum nächsten Wechsel.\r\n"
            L"# Zeilen mit # davor sind Kommentare. Speichern genügt, Lumen lädt die Datei sofort neu.\r\n"
            L"# Einschalten: Menü > Einstellungen > Zeitplan.\r\n"
            L"#\r\n"
            L"# Beispiel (ohne # davor aktivieren):\r\n"
            L"# 06:00 | Beispiele\\Aurora\r\n"
            L"# 18:00 | Beispiele\\Spektrum\r\n"
            L"#\r\n"
            L"# Verfügbare Hintergründe (\"none\" schaltet den Live-Hintergrund aus):\r\n";
        int n = 0;
        for (auto& w : lib_.Items()) {
            if (++n > 200) break;
            t += L"#   " + w.id + L"   (" + w.name + L")\r\n";
        }
        WriteTextFile(path, WideToUtf8(t));
    }
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    Balloon(L"Zeitplan", cfg_.GetBool(L"schedule", false) ? L"Speichern genügt – Lumen lädt den Zeitplan sofort neu."
                                                         : L"Der Zeitplan ist noch aus: Menü → Einstellungen → Zeitplan.");
}

const Screen* App::PropertyScreen() const {
    for (auto& s : screens_)
        if (s.hasWall && !s.params.empty() && !s.wp.iniPath.empty()) return &s;
    return nullptr;
}

// Öffnet die wallpaper.ini im Editor; fehlende Werte werden mit ihrer Vorgabe eingetragen
void App::EditProperties() {
    const Screen* s = PropertyScreen();
    if (!s) return;
    std::string text;
    ReadTextFile(s->wp.iniPath, text);
    auto have = ParseIni(text);
    std::wstring add;
    for (auto& p : s->params) {
        if (have.count(L"param." + ToLower(p.name))) continue;
        wchar_t val[32];
        if (p.color) swprintf(val, 32, L"#%02X%02X%02X", (int)std::lround(p.def[0] * 255), (int)std::lround(p.def[1] * 255),
                              (int)std::lround(p.def[2] * 255));
        else swprintf(val, 32, L"%g", p.def[0]);
        wchar_t range[48] = L"";
        if (!p.color) swprintf(range, 48, L" (%g bis %g)", p.mn, p.mx);
        add += L"# " + (p.label.empty() ? p.name : p.label) + range + L"\r\nparam." + p.name + L"=" + val + L"\r\n";
    }
    if (!add.empty()) {
        if (!text.empty() && text.back() != '\n') text += "\r\n";
        text += WideToUtf8(add);
        WriteTextFile(s->wp.iniPath, text);
    }
    std::wstring ini = s->wp.iniPath;
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + ini + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    Balloon(L"Eigenschaften", L"Werte ändern und speichern – Lumen lädt sofort neu.");
}

void App::OpenConfig() {
    cfg_.Save();
    std::wstring p = JoinPath(AppDataDir(), L"config.ini");
    if (cfg_.Get(L"catalog_url").empty()) {
        // Platzhalter, damit man sieht, wo die Adresse hingehört
        std::string text;
        ReadTextFile(p, text);
        if (text.find("catalog_url") == std::string::npos) {
            text += "# Adresse eines Online-Katalogs (Textdatei, eine Zeile pro Eintrag: Name | https://…/datei.zip | Beschreibung)\r\n"
                    "catalog_url=\r\n";
            WriteTextFile(p, text);
        }
    }
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + p + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    Balloon(L"Einstellungen", L"Nach dem Speichern: Menü → Einstellungen → Neu laden.");
}

void App::RefreshCatalog() {
    std::wstring url = cfg_.Get(L"catalog_url");
    if (url.empty()) {
        OpenConfig();
        return;
    }
    if (catBusy_) return;
    catBusy_ = true;
    HWND wnd = msgWnd_;
    std::thread([this, url, wnd] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::vector<CatalogItem> items;
        std::wstring err;
        bool ok = FetchCatalog(url, items, err);
        {
            std::lock_guard<std::mutex> lk(catMtx_);
            if (ok) catalog_ = items;
            catErr_ = err;
        }
        CoUninitialize();
        PostMessageW(wnd, WM_APP_CATALOG, ok, 0);
    }).detach();
}

void App::InstallCatalog(size_t idx) {
    CatalogItem item;
    {
        std::lock_guard<std::mutex> lk(catMtx_);
        if (idx >= catalog_.size()) return;
        item = catalog_[idx];
    }
    if (catBusy_) return;
    catBusy_ = true;
    Balloon(L"Lade herunter…", item.name);
    HWND wnd = msgWnd_;
    std::thread([this, item, wnd] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::wstring rel, err;
        bool ok = InstallCatalogItem(item, rel, err);
        {
            std::lock_guard<std::mutex> lk(catMtx_);
            installRel_ = rel;
            installErr_ = err;
        }
        CoUninitialize();
        PostMessageW(wnd, WM_APP_INSTALLED, ok, 0);
    }).detach();
}

void App::Quit() {
    settings_.reset();   // vor dem Grafikgerät: das Fenster hält Bilder und Texturen
    KillTimer(msgWnd_, TIMER_CHECK);
    audio_.Stop();
    if (hook_) UnhookWinEvent(hook_);
    DestroyScreens();
    gfx_.Shutdown();
    RefreshSystemWallpaper();
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    Log(L"Lumen beendet");
    PostQuitMessage(0);
}

// =====================================================================================
//  Einstieg
// =====================================================================================
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    // Physische Pixel auf allen Bildschirmen (zusätzlich zum Manifest)
    using SetCtx = BOOL(WINAPI*)(HANDLE);
    if (auto fn = (SetCtx)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4 /*PER_MONITOR_AWARE_V2*/);

    std::wstring file;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1 && FileExists(argv[1])) {
        wchar_t full[MAX_PATH * 4];
        GetFullPathNameW(argv[1], MAX_PATH * 4, full, nullptr);
        file = full;
    }
    if (argv) LocalFree(argv);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Lumen.Wallpaper.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"LumenTray", nullptr);
        if (other) {
            if (!file.empty()) {
                COPYDATASTRUCT cd{0x4C554D, (DWORD)((file.size() + 1) * sizeof(wchar_t)), (void*)file.c_str()};
                SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_NORMAL, 5000, nullptr);
            } else {
                PostMessageW(other, WM_APP_HELLO, 0, 0);
            }
        }
        return 0;
    }
    App app;
    int r = app.Run(inst, file);
    if (mutex) CloseHandle(mutex);
    return r;
}
