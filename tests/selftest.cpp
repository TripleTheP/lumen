// Lumen – Selbsttest (nur unter Windows ausführbar): make test
// Prüft Katalog-Installation, Vollbild-Erkennung und dass alle mitgelieferten Shader kompilieren.
// Achtung: Arbeitet mit der echten Bibliothek (%LOCALAPPDATA%\Lumen) und räumt danach auf.

#include "common.h"
#include "library.h"
#include "desktop.h"
#include "renderer.h"
#include "examples.h"
#include <shellapi.h>
#include <thread>

static int fails = 0;
#define CHECK(c) do { bool ok_ = (c); wprintf(L"%ls  %hs\n", ok_ ? L"OK  " : L"FEHLER", #c); if (!ok_) fails++; } while (0)

static LRESULT CALLBACK TestWndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static void Pump() {
    MSG msg;
    for (int i = 0; i < 20; i++) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(15);
    }
}

static void TestCatalogInstall() {
    wprintf(L"-- Katalog: Installieren und Neuinstallieren\n");
    std::wstring dest = JoinPath(JoinPath(LibraryDir(), L"Online"), L"Lavalampe");
    std::wstring work = JoinPath(DirOf(LibraryDir()), L"Download");
    if (DirExists(dest)) {
        wprintf(L"SKIP  %ls existiert bereits – wird nicht angefasst\n", dest.c_str());
        return;
    }
    wchar_t zip[MAX_PATH];
    GetFullPathNameW(L"catalog\\pakete\\Lavalampe.zip", MAX_PATH, zip, nullptr);
    CHECK(FileExists(zip));
    CatalogItem item{L"Lavalampe", L"file:///" + std::wstring(zip), L""};
    std::wstring rel, err;

    CHECK(InstallCatalogItem(item, rel, err));
    CHECK(rel == L"Online\\Lavalampe");
    std::wstring ini = JoinPath(dest, L"wallpaper.ini");
    CHECK(FileExists(ini));

    // Neuinstallation ersetzt den Ordner ganz: alte Dateien verschwinden
    std::wstring stale = JoinPath(dest, L"veraltet.txt");
    WriteTextFile(stale, "alt");
    CHECK(InstallCatalogItem(item, rel, err));
    CHECK(!FileExists(stale));
    CHECK(!DirExists(JoinPath(work, L"Lavalampe")) && !DirExists(JoinPath(work, L"Lavalampe.alt")));

    // Gesperrte Datei (wie ein laufendes Video): Abbruch, nichts geht kaputt
    HANDLE lock = CreateFileW(ini.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    err.clear();
    CHECK(!InstallCatalogItem(item, rel, err));
    CHECK(!err.empty());
    CHECK(FileExists(ini));
    CHECK(!DirExists(JoinPath(work, L"Lavalampe")));
    CloseHandle(lock);

    // Aufräumen
    std::wstring from = dest + L'\0';
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
    RemoveDirectoryW(JoinPath(LibraryDir(), L"Online").c_str());   // nur wenn leer
    RemoveDirectoryW(work.c_str());
}

static void TestFullscreen() {
    wprintf(L"-- Vollbild-Erkennung (auf dem Hauptbildschirm blitzt kurz ein schwarzes Fenster auf)\n");
    WNDCLASSW wc{};
    wc.lpfnWndProc = TestWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LumenSelftest";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);
    POINT origin{0, 0};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT r = mi.rcMonitor;

    HWND fs = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Vollbild", WS_POPUP | WS_VISIBLE, r.left, r.top,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    SetForegroundWindow(fs);
    Pump();
    if (GetForegroundWindow() != fs) {
        wprintf(L"SKIP  Testfenster bekam keinen Fokus (Windows-Fokussperre)\n");
        DestroyWindow(fs);
        return;
    }
    CHECK(IsForegroundFullscreen());

    HWND win = CreateWindowExW(0, wc.lpszClassName, L"Fenster", WS_OVERLAPPEDWINDOW, r.left, r.top, 400, 300, nullptr,
                               nullptr, wc.hInstance, nullptr);
    DestroyWindow(fs);
    ShowWindow(win, SW_SHOWMAXIMIZED);
    SetForegroundWindow(win);
    Pump();
    if (GetForegroundWindow() == win) CHECK(!IsForegroundFullscreen());   // maximiert ist kein Vollbild
    ShowWindow(win, SW_RESTORE);
    Pump();
    if (GetForegroundWindow() == win) CHECK(!IsForegroundFullscreen());
    DestroyWindow(win);
}

static void TestShaders() {
    wprintf(L"-- Mitgelieferte Shader kompilieren und zeichnen\n");
    Gfx g;
    if (!g.Init(false)) {
        wprintf(L"SKIP  kein Direct3D 11\n");
        return;
    }
    D3D11_TEXTURE2D_DESC d{};
    d.Width = 320;
    d.Height = 180;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11RenderTargetView> rtv;
    g.Dev()->CreateTexture2D(&d, nullptr, &tex);
    g.Dev()->CreateRenderTargetView(tex.Get(), nullptr, &rtv);
    ShaderConstants c{};
    c.resolution[0] = 320;
    c.resolution[1] = 180;

    std::vector<std::pair<std::wstring, std::string>> all;
    for (auto& ex : Examples()) all.push_back({ex.folder, ex.shader});
    all.push_back({L"Vorlage", ShaderTemplate()});
    for (auto& s : all) {
        std::wstring err;
        auto p = g.GetProgram(s.second, 0, err);
        wprintf(L"%ls  %ls%ls%ls\n", p ? L"OK  " : L"FEHLER", s.first.c_str(), err.empty() ? L"" : L": ", err.c_str());
        if (!p) { fails++; continue; }
        g.Draw(rtv.Get(), 320, 180, p.get(), nullptr, c);
    }
    g.Shutdown();
}

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::thread([] {   // wie in Lumen: Installation läuft in einem eigenen MTA-Thread
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        TestCatalogInstall();
        CoUninitialize();
    }).join();
    TestFullscreen();
    TestShaders();
    wprintf(L"\n%ls (%d Fehler)\n", fails ? L"FEHLGESCHLAGEN" : L"ALLES OK", fails);
    return fails ? 1 : 0;
}
