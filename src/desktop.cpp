#include "desktop.h"
#include <dwmapi.h>
#include <algorithm>

static BOOL CALLBACK MonEnumProc(HMONITOR hm, HDC, LPRECT, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hm, &mi)) {
        MonitorInfo m;
        m.device = mi.szDevice;
        m.rc = mi.rcMonitor;
        m.work = mi.rcWork;
        m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        v->push_back(m);
    }
    return TRUE;
}

std::vector<MonitorInfo> EnumMonitors() {
    std::vector<MonitorInfo> v;
    EnumDisplayMonitors(nullptr, nullptr, MonEnumProc, (LPARAM)&v);
    // Primärer zuerst, danach von links nach rechts
    std::stable_sort(v.begin(), v.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.rc.left != b.rc.left) return a.rc.left < b.rc.left;
        return a.rc.top < b.rc.top;
    });
    return v;
}

static std::wstring ClassOf(HWND h) {
    wchar_t buf[128] = {};
    GetClassNameW(h, buf, 127);
    return buf;
}

struct FindWorkerCtx {
    HWND workerw = nullptr;
};

static BOOL CALLBACK FindWorkerProc(HWND top, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindWorkerCtx*>(lp);
    HWND defview = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defview) {
        // Der WorkerW direkt hinter dem Fenster mit den Symbolen ist unser Ziel
        ctx->workerw = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
    }
    return TRUE;
}

bool DesktopHost::Attach(bool layered) {
    layered_ = layered;
    progman_ = FindWindowW(L"Progman", nullptr);
    parent_ = workerw_ = defview_ = nullptr;
    fallback_ = false;
    if (!progman_) {
        // Kein Explorer-Desktop (andere Shell): Fenster ganz unten als Ersatz
        Log(L"Desktop: Progman nicht gefunden – Ersatzmodus");
        fallback_ = true;
        return false;
    }
    DWORD_PTR res = 0;
    // Bittet Explorer, einen WorkerW hinter den Symbolen zu erzeugen
    SendMessageTimeoutW(progman_, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &res);
    SendMessageTimeoutW(progman_, 0x052C, 0, 0, SMTO_NORMAL, 1000, &res);

    workerw_ = FindWindowExW(progman_, nullptr, L"WorkerW", nullptr);
    defview_ = FindWindowExW(progman_, nullptr, L"SHELLDLL_DefView", nullptr);
    if (workerw_) {
        // Windows 11 24H2 und neuer
        raised_ = true;
        parent_ = progman_;
        Log(L"Desktop: Modus 24H2 (WorkerW unter Progman)");
        return true;
    }

    raised_ = false;
    FindWorkerCtx ctx;
    EnumWindows(FindWorkerProc, (LPARAM)&ctx);
    if (ctx.workerw) {
        workerw_ = ctx.workerw;
        parent_ = workerw_;
        Log(L"Desktop: klassischer WorkerW-Modus");
        return true;
    }
    // Letzter Ausweg: direkt in Progman unter die Symbole
    parent_ = progman_;
    raised_ = true;
    Log(L"Desktop: Fallback in Progman");
    return true;
}

bool DesktopHost::IsValid() const {
    if (fallback_) return true;
    return parent_ && IsWindow(parent_) && (!workerw_ || IsWindow(workerw_));
}

bool DesktopHost::ExplorerAppeared() const {
    return fallback_ && FindWindowW(L"Progman", nullptr) != nullptr;
}

void DesktopHost::Place(HWND child, const RECT& r) {
    if (fallback_ || !parent_) {
        SetWindowPos(child, HWND_BOTTOM, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }
    if (GetParent(child) != parent_) {
        SetParent(child, parent_);
        LONG_PTR st = GetWindowLongPtrW(child, GWL_STYLE);
        st = (st & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME)) | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        SetWindowLongPtrW(child, GWL_STYLE, st);
        if (layered_) {
            LONG_PTR ex = GetWindowLongPtrW(child, GWL_EXSTYLE);
            SetWindowLongPtrW(child, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            SetLayeredWindowAttributes(child, 0, 255, LWA_ALPHA);
        }
    }
    POINT tl{r.left, r.top};
    MapWindowPoints(HWND_DESKTOP, parent_, &tl, 1);
    int w = r.right - r.left, h = r.bottom - r.top;
    HWND after = HWND_TOP;
    if (raised_) after = defview_ ? defview_ : HWND_BOTTOM;
    SetWindowPos(child, after, tl.x, tl.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    if (raised_ && workerw_) {
        // Der Explorer-Hintergrund muss unter uns liegen
        SetWindowPos(workerw_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

bool IsPointOverDesktop(POINT pt) {
    HWND h = WindowFromPoint(pt);
    if (!h) return false;
    HWND root = GetAncestor(h, GA_ROOT);
    std::wstring c = ClassOf(root ? root : h);
    return c == L"Progman" || c == L"WorkerW" || c == L"LumenScreen";
}

struct CoverCtx {
    RECT work;
    bool covered = false;
};

static bool IsShellWindow(HWND h) {
    std::wstring c = ClassOf(h);
    return c == L"Progman" || c == L"WorkerW" || c == L"Shell_TrayWnd" || c == L"Shell_SecondaryTrayWnd" ||
           c == L"LumenScreen" || c == L"LumenTray" || c == L"Windows.UI.Core.CoreWindow" ||
           c == L"XamlExplorerHostIslandWindow" || c == L"TopLevelWindowForOverflowXamlIsland";
}

static BOOL CALLBACK CoverProc(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<CoverCtx*>(lp);
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return TRUE;  // durchklickbare Overlays
    if (ex & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        COLORREF key = 0;
        if (GetLayeredWindowAttributes(h, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha < 250) return TRUE;
    }
    if (IsShellWindow(h)) return TRUE;
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
    RECT in{};
    if (!IntersectRect(&in, &r, &ctx->work)) return TRUE;
    double a = double(in.right - in.left) * double(in.bottom - in.top);
    double b = double(ctx->work.right - ctx->work.left) * double(ctx->work.bottom - ctx->work.top);
    if (b > 0 && a >= 0.97 * b) {
        ctx->covered = true;
        return FALSE;
    }
    return TRUE;
}

bool IsMonitorCovered(const MonitorInfo& m) {
    CoverCtx ctx;
    ctx.work = m.work;
    EnumWindows(CoverProc, (LPARAM)&ctx);
    return ctx.covered;
}

void RefreshSystemWallpaper() {
    wchar_t path[MAX_PATH * 2] = {};
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH * 2, path, 0);
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, path, SPIF_SENDCHANGE);
}
