// Lumen – Selbsttest (nur unter Windows ausführbar): make test
// Prüft Katalog-Installation, Vollbild-Erkennung und dass alle mitgelieferten Shader kompilieren.
// Achtung: Arbeitet mit der echten Bibliothek (%LOCALAPPDATA%\Lumen) und räumt danach auf.

#include "common.h"
#include "library.h"
#include "desktop.h"
#include "renderer.h"
#include "examples.h"
#include "schedule.h"
#include "settings.h"
#include <set>
#include <shellapi.h>
#include <wincodec.h>
#include <thread>
#include <cmath>
#include <algorithm>

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

// Werte der Eigenschaften eines Shaders (Vorgaben, überschrieben durch ini) in die Konstanten eintragen
static void FillParams(ShaderConstants& c, const std::string& shader,
                       const std::map<std::wstring, std::wstring>& ini = {}) {
    auto ps = ParseShaderParams(shader);
    for (size_t i = 0; i < ps.size(); i++) ParamValue(ps[i], ini, c.params[i]);
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
        wprintf(L"%ls  %ls%ls%ls   (Fnv1a %016llX)\n", p ? L"OK  " : L"FEHLER", s.first.c_str(), err.empty() ? L"" : L": ",
                err.c_str(), (unsigned long long)Fnv1a(s.second));   // für ExampleWallpaper::replaces
        if (!p) { fails++; continue; }
        FillParams(c, s.second);
        g.Draw(rtv.Get(), 320, 180, p.get(), nullptr, c);
    }
    g.Shutdown();
}

struct Target {
    ComPtr<ID3D11Texture2D> tex, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    int w = 0, h = 0;
};

static Target MakeTarget(Gfx& g, int w, int h) {
    Target t;
    t.w = w;
    t.h = h;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    g.Dev()->CreateTexture2D(&d, nullptr, &t.tex);
    g.Dev()->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    g.Dev()->CreateTexture2D(&d, nullptr, &t.staging);
    return t;
}

// BGRA-Bytes des Ziels
static std::vector<uint8_t> ReadPixels(Gfx& g, Target& t) {
    g.Ctx()->CopyResource(t.staging.Get(), t.tex.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    g.Ctx()->Map(t.staging.Get(), 0, D3D11_MAP_READ, 0, &m);
    std::vector<uint8_t> px((size_t)t.w * t.h * 4);
    for (int y = 0; y < t.h; y++)
        memcpy(&px[(size_t)y * t.w * 4], (uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)t.w * 4);
    g.Ctx()->Unmap(t.staging.Get(), 0);
    return px;
}

// Panorama: Jeder Bildschirm zeichnet seinen Ausschnitt des gemeinsamen Desktop-Rechtecks. Die Kacheln müssen
// zusammen exakt das Bild ergeben, das ein einziger großer Bildschirm zeigen würde.
static void TestPanorama() {
    wprintf(L"-- Panorama: Kacheln ergeben das Gesamtbild\n");
    Gfx g;
    if (!g.Init(false)) {
        wprintf(L"SKIP  kein Direct3D 11\n");
        return;
    }
    const int W = 640, H = 360;
    ShaderConstants whole{};
    whole.resolution[0] = W;
    whole.resolution[1] = H;
    whole.time = 123.4f;
    whole.audio[0] = 0.7f;
    whole.mouse[0] = 500;
    whole.mouse[1] = 100;
    whole.mouse[3] = 1;
    whole.click[2] = 10000;

    std::string aurora;
    for (auto& ex : Examples())
        if (std::wstring(ex.folder) == L"Aurora") aurora = ex.shader;
    FillParams(whole, aurora);
    std::wstring err;
    auto pa = g.GetProgram(aurora, 0, err);
    auto grad = g.GetProgram("float4 mainImage(float2 p) { return float4(p / iResolution, 0, 1); }", 0, err);
    CHECK(pa && grad);
    if (!pa || !grad) return;

    // Gesamtbild
    Target full = MakeTarget(g, W, H);
    g.Draw(full.rtv.Get(), W, H, pa.get(), nullptr, whole);
    auto ref = ReadPixels(g, full);
    int distinct = 0;   // Vergleichsbild darf nicht einfarbig sein, sonst beweist der Kachelvergleich nichts
    for (size_t i = 4; i < ref.size() && distinct < 100; i += 4) distinct += ref[i] != ref[0] || ref[i + 1] != ref[1];
    CHECK(distinct >= 100);

    // Versetzte Kacheln wie bei übereinander gestellten Bildschirmen: links unten und rechts oben
    struct Tile { int x, y, w, h; } tiles[] = {{0, 180, 320, 180}, {320, 0, 320, 180}, {0, 0, 320, 180}};
    int maxDiff = 0;
    for (auto& t : tiles) {
        ShaderConstants c = whole;
        c.map[0] = (float)t.x;
        c.map[1] = (float)t.y;
        Target tt = MakeTarget(g, t.w, t.h);
        g.Draw(tt.rtv.Get(), t.w, t.h, pa.get(), nullptr, c);
        auto px = ReadPixels(g, tt);
        for (int y = 0; y < t.h; y++)
            for (int x = 0; x < t.w; x++)
                for (int k = 0; k < 4; k++)
                    maxDiff = std::max(maxDiff, std::abs(int(px[((size_t)y * t.w + x) * 4 + k]) -
                                                         int(ref[((size_t)(y + t.y) * W + (x + t.x)) * 4 + k])));
    }
    CHECK(maxDiff == 0);

    // Kleinere Renderauflösung (halb): fragCoord muss trotzdem die echte Desktop-Position sein
    ShaderConstants c = whole;
    c.map[0] = 320;
    c.map[1] = 180;
    c.map[2] = c.map[3] = 2;   // 160x90 Bildpunkte decken 320x180 Desktop-Pixel ab
    Target half = MakeTarget(g, 160, 90);
    g.Draw(half.rtv.Get(), 160, 90, grad.get(), nullptr, c);
    auto px = ReadPixels(g, half);
    int worst = 0;
    for (int y = 0; y < 90; y += 11)
        for (int x = 0; x < 160; x += 13) {
            float ex = ((x + 0.5f) * 2 + 320) / W * 255.0f, ey = ((y + 0.5f) * 2 + 180) / H * 255.0f;
            const uint8_t* p = &px[((size_t)y * 160 + x) * 4];   // B G R A
            worst = std::max(worst, (int)std::lround(std::fabs(p[2] - ex) * 10));
            worst = std::max(worst, (int)std::lround(std::fabs(p[1] - ey) * 10));
        }
    CHECK(worst <= 10);   // höchstens 1/255 Rundungsfehler
    g.Shutdown();
}

static void TestIniAndParams() {
    wprintf(L"-- wallpaper.ini und Eigenschaften\n");
    // Kommentare hinter dem Wert gehören nicht zum Wert; ein ';' ohne Leerraum davor schon (Pfade)
    auto kv = ParseIni("name=Test   ; ein Kommentar\r\nfit=cover\r\nsource=a;b.mp4\r\n# Kommentar\r\nParam.Farbe=#FF8000\r\n");
    CHECK(kv[L"name"] == L"Test");
    CHECK(kv[L"fit"] == L"cover");
    CHECK(kv[L"source"] == L"a;b.mp4");
    CHECK(kv[L"param.farbe"] == L"#FF8000");   // Schlüssel kleingeschrieben, '#' im Wert bleibt

    std::string shader =
        "// @param tempo float 1.5 0.5 3 Geschwindigkeit der Wellen\n"
        "  //   @param farbe color #33FF99 Hauptfarbe\n"   // Leerraum vor und nach // ist erlaubt
        "// @param kaputt float abc\n"                       // ungültig: wird ignoriert
        "// @param 9x float 1 0 2\n"                         // ungültiger Name
        "// @param tempo float 1 0 2\n"                      // doppelt
        "float4 mainImage(float2 p) { return float4(tempo / 3.0, farbe.g, farbe.b, 1); }\n";
    auto ps = ParseShaderParams(shader);
    CHECK(ps.size() == 2);
    if (ps.size() != 2) return;
    CHECK(ps[0].name == L"tempo" && !ps[0].color && ps[0].mn == 0.5f && ps[0].mx == 3.0f && ps[0].def[0] == 1.5f);
    CHECK(ps[0].label == L"Geschwindigkeit der Wellen");
    CHECK(ps[1].name == L"farbe" && ps[1].color);

    float v[4];
    ParamValue(ps[0], {}, v);
    CHECK(v[0] == 1.5f);                                           // Vorgabe
    ParamValue(ps[0], {{L"param.tempo", L"2,25"}}, v);
    CHECK(v[0] == 2.25f);                                          // deutsches Komma
    ParamValue(ps[0], {{L"param.tempo", L"99"}}, v);
    CHECK(v[0] == 3.0f);                                           // auf Höchstwert begrenzt
    ParamValue(ps[0], {{L"param.tempo", L"viel"}}, v);
    CHECK(v[0] == 1.5f);                                           // Unsinn: Vorgabe bleibt
    ParamValue(ps[1], {{L"param.farbe", L"#FF8000"}}, v);
    CHECK(v[0] == 1.0f && std::fabs(v[1] - 128 / 255.0f) < 1e-6 && v[2] == 0.0f);
    ParamValue(ps[1], {{L"param.farbe", L"rot"}}, v);
    CHECK(std::fabs(v[0] - 0x33 / 255.0f) < 1e-6);                 // ungültige Farbe: Vorgabe

    std::string many;
    for (int i = 0; i < 20; i++) many += "// @param p" + std::to_string(i) + " float 1 0 2\n";
    CHECK((int)ParseShaderParams(many).size() == kMaxParams);

    Gfx g;
    if (!g.Init(false)) {
        wprintf(L"SKIP  kein Direct3D 11\n");
        return;
    }
    std::wstring err;
    auto prog = g.GetProgram(shader, 0, err);
    CHECK(prog != nullptr);
    if (!prog) { wprintf(L"      %ls\n", err.c_str()); return; }
    Target t = MakeTarget(g, 4, 4);
    ShaderConstants c{};
    c.resolution[0] = c.resolution[1] = 4;
    FillParams(c, shader, {{L"param.tempo", L"3"}, {L"param.farbe", L"#00FF00"}});
    g.Draw(t.rtv.Get(), 4, 4, prog.get(), nullptr, c);
    auto px = ReadPixels(g, t);   // B G R A
    CHECK(px[2] == 255 && px[1] == 255 && px[0] == 0);   // tempo/3 = 1, farbe = grün (g=1, b=0)
    FillParams(c, shader, {{L"param.tempo", L"0,75"}, {L"param.farbe", L"#003366"}});
    g.Draw(t.rtv.Get(), 4, 4, prog.get(), nullptr, c);
    px = ReadPixels(g, t);
    CHECK(px[2] == 64 && px[1] == 51 && px[0] == 102);   // R = 0,75/3 = 0,25 -> 64; G = 0x33 -> 51; B = 0x66 -> 102
    g.Shutdown();
}

static void TestSchedule() {
    wprintf(L"-- Zeitplan\n");
    auto plan = ParseSchedule(
        "# Kommentar\r\n"
        "18:00 | Beispiele\\Spektrum\r\n"
        "6:30 | Beispiele\\Aurora   \r\n"    // einstellige Stunde, Leerraum am Ende
        "25:00 | zuspaet\r\n"                 // ungültig
        "12:5 | kurz\r\n"                     // ungültig
        "12:00 |\r\n"                         // ohne Hintergrund
        "keine Zeile\r\n"
        "18:00 | Beispiele\\Punktfeld\r\n"    // gleiche Uhrzeit: spätere Zeile gewinnt
        "23:59 | none\r\n");
    CHECK(plan.size() == 3);
    if (plan.size() != 3) return;
    CHECK(plan[0].minute == 6 * 60 + 30 && plan[0].id == L"Beispiele\\Aurora");   // sortiert
    CHECK(plan[1].minute == 18 * 60 && plan[1].id == L"Beispiele\\Punktfeld");
    CHECK(plan[2].minute == 23 * 60 + 59 && plan[2].id == L"none");

    auto at = [&](int h, int m) { return ActiveScheduleEntry(plan, h * 60 + m)->id; };
    CHECK(at(6, 30) == L"Beispiele\\Aurora");        // genau zur Wechselzeit
    CHECK(at(6, 29) == L"none");                     // davor gilt noch der letzte von gestern
    CHECK(at(0, 0) == L"none");                      // über Mitternacht
    CHECK(at(12, 0) == L"Beispiele\\Aurora");
    CHECK(at(18, 0) == L"Beispiele\\Punktfeld");
    CHECK(at(23, 58) == L"Beispiele\\Punktfeld");
    CHECK(at(23, 59) == L"none");
    CHECK(ActiveScheduleEntry({}, 100) == nullptr);
    auto one = ParseSchedule("08:00 | Beispiele\\Aurora\n");   // ein einziger Eintrag gilt immer
    CHECK(ActiveScheduleEntry(one, 3)->id == L"Beispiele\\Aurora" && ActiveScheduleEntry(one, 900)->id == L"Beispiele\\Aurora");
    CHECK(ParseSchedule("").empty());
}

// Schreibt ein GIF mit drei einfarbigen Bildern (rot, grün, blau, je 8x8, 100 ms)
static bool WriteTestGif(const std::wstring& path) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    wic->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    wic->CreateEncoder(GUID_ContainerFormatGif, nullptr, &enc);
    if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    const WICColor colors[3] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    for (WICColor color : colors) {
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;
        if (FAILED(enc->CreateNewFrame(&frame, &props)) || FAILED(frame->Initialize(props.Get()))) return false;
        frame->SetSize(8, 8);
        GUID pf = GUID_WICPixelFormat8bppIndexed;
        frame->SetPixelFormat(&pf);
        ComPtr<IWICPalette> pal;
        wic->CreatePalette(&pal);
        pal->InitializeCustom((WICColor*)&color, 1);
        frame->SetPalette(pal.Get());
        ComPtr<IWICMetadataQueryWriter> qw;
        if (SUCCEEDED(frame->GetMetadataQueryWriter(&qw))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            v.vt = VT_UI2;
            v.uiVal = 10;   // Hundertstel Sekunden
            qw->SetMetadataByName(L"/grctlext/Delay", &v);
        }
        std::vector<uint8_t> idx(8 * 8, 0);
        if (FAILED(frame->WritePixels(8, 8, (UINT)idx.size(), idx.data())) || FAILED(frame->Commit())) return false;
    }
    return SUCCEEDED(enc->Commit()) && SUCCEEDED(stream->Commit(STGC_DEFAULT));
}

// Farbe (R, G, B) des Bildpunkts x in der ersten Zeile einer Quelle
static void FirstPixel(Gfx& g, Source& src, int rgb[3], int x = 0) {
    ComPtr<ID3D11Resource> res;
    src.Srv()->GetResource(&res);
    ComPtr<ID3D11Texture2D> tex;
    res.As(&tex);
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> st;
    g.Dev()->CreateTexture2D(&d, nullptr, &st);
    g.Ctx()->CopyResource(st.Get(), tex.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    g.Ctx()->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m);
    const uint8_t* p = (const uint8_t*)m.pData + x * 4;   // B G R A
    rgb[0] = p[2];
    rgb[1] = p[1];
    rgb[2] = p[0];
    g.Ctx()->Unmap(st.Get(), 0);
}

static void TestGif() {
    wprintf(L"-- Animiertes GIF\n");
    Gfx g;
    if (!g.Init(false)) {
        wprintf(L"SKIP  kein Direct3D 11\n");
        return;
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"lumen_selftest.gif";
    bool wrote = WriteTestGif(path);
    CHECK(wrote);
    if (!wrote) return;
    std::wstring err;
    auto src = LoadImageSource(g, path, err);
    CHECK(src != nullptr);
    if (!src) { wprintf(L"      %ls\n", err.c_str()); DeleteFileW(path.c_str()); return; }
    CHECK(src->Animated() && src->Width() == 8 && src->Height() == 8);
    int c[3];
    auto is = [&](int r, int gr, int b) { FirstPixel(g, *src, c); return c[0] == r && c[1] == gr && c[2] == b; };
    CHECK(is(255, 0, 0));                       // erstes Bild: rot
    src->Update(10.0, true);                    // legt den Takt fest, Bild bleibt
    CHECK(is(255, 0, 0) && std::fabs(src->NextChange() - 10.1) < 1e-9);
    src->Update(10.05, true);
    CHECK(is(255, 0, 0));                       // noch nicht fällig
    src->Update(10.11, true);
    CHECK(is(0, 255, 0));                       // grün
    src->Update(10.21, true);
    CHECK(is(0, 0, 255));                       // blau
    src->Update(10.31, true);
    CHECK(is(255, 0, 0));                       // und wieder von vorn
    src->Update(500.0, true);                   // nach langer Pause: nur ein Bild weiter, nicht aufholen
    CHECK(is(0, 255, 0) && src->NextChange() >= 500.0);
    src->SetSpeed(2.0);
    double before = src->NextChange();
    src->Update(before, true);
    CHECK(std::fabs(src->NextChange() - (before + 0.05)) < 1e-9);   // doppeltes Tempo: 50 ms pro Bild
    src.reset();
    DeleteFileW(path.c_str());
    g.Shutdown();
}

// Von Hand gebautes GIF, 2x1 Punkte, drei Teilbilder (je 1x1) mit unterschiedlicher Entsorgung:
//   A: links rot,  Entsorgung 1 (stehen lassen)
//   B: rechts grün, Entsorgung 2 (Fläche leeren)
//   C: links blau, Entsorgung 1
// Gezeigt wird also: (rot, -) -> (rot, grün) -> (blau, -): grün verschwindet, weil B entsorgt wird, rot wird übermalt.
static void TestGifPartialFrames() {
    wprintf(L"-- GIF mit Teilbildern und Entsorgung\n");
    Gfx g;
    if (!g.Init(false)) {
        wprintf(L"SKIP  kein Direct3D 11\n");
        return;
    }
    const uint8_t gif[] = {
        'G', 'I', 'F', '8', '9', 'a', 2, 0, 1, 0, 0x91, 0, 0,                      // Kopf, Leinwand 2x1, 4 Farben
        0xFF, 0x00, 0x00,  0x00, 0xFF, 0x00,  0x00, 0x00, 0xFF,  0x00, 0x00, 0x00,  // rot, grün, blau, schwarz
        0x21, 0xF9, 4, 0x04, 10, 0, 0, 0,  0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0,  2, 2, 0x44, 0x01, 0,   // A
        0x21, 0xF9, 4, 0x08, 10, 0, 0, 0,  0x2C, 1, 0, 0, 0, 1, 0, 1, 0, 0,  2, 2, 0x4C, 0x01, 0,   // B
        0x21, 0xF9, 4, 0x04, 10, 0, 0, 0,  0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0,  2, 2, 0x54, 0x01, 0,   // C
        0x3B};
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"lumen_selftest2.gif";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    CHECK(f != INVALID_HANDLE_VALUE && WriteFile(f, gif, (DWORD)sizeof(gif), &written, nullptr));
    CloseHandle(f);
    std::wstring err;
    auto src = LoadImageSource(g, path, err);
    CHECK(src != nullptr && src->Animated());
    if (!src) { DeleteFileW(path.c_str()); return; }
    CHECK(src->Width() == 2 && src->Height() == 1);
    int l[3], r[3];
    auto now = [&](int lr, int lg, int lb, int rr, int rg, int rb) {
        FirstPixel(g, *src, l, 0);
        FirstPixel(g, *src, r, 1);
        return l[0] == lr && l[1] == lg && l[2] == lb && r[0] == rr && r[1] == rg && r[2] == rb;
    };
    CHECK(now(255, 0, 0, 0, 0, 0));        // A: links rot, rechts leer (schwarz)
    src->Update(1.0, true);
    src->Update(1.11, true);
    CHECK(now(255, 0, 0, 0, 255, 0));      // B: rot bleibt stehen, rechts grün
    src->Update(1.21, true);
    CHECK(now(0, 0, 255, 0, 0, 0));        // C: B wurde entsorgt (grün weg), links blau
    src->Update(1.31, true);
    CHECK(now(255, 0, 0, 0, 0, 0));        // Schleife: Leinwand wird für Bild A geleert
    src.reset();
    DeleteFileW(path.c_str());
    g.Shutdown();
}

static void TestSettingsHelpers() {
    wprintf(L"-- Einstellungsfenster: ini schreiben, Vorschaubilder\n");
    // SetIniValue ersetzt nur die eine Zeile (Groß-/Kleinschreibung egal) und lässt Kommentare und Zeilenenden in Ruhe
    std::string ini = "name=Test\r\n# Kommentar mit param.tempo=9\r\nParam.Tempo=1 ; alt\r\nfit=cover\r\n";
    std::string out = SetIniValueInText(ini, L"param.tempo", L"2.5");
    CHECK(out == "name=Test\r\n# Kommentar mit param.tempo=9\r\nParam.Tempo=2.5\r\nfit=cover\r\n");
    CHECK(SetIniValueInText(out, L"param.farbe", L"#FF0000") == out + "param.farbe=#FF0000\r\n");   // fehlt: angehängt
    CHECK(SetIniValueInText("a=1", L"b", L"2") == "a=1\r\nb=2\r\n");                                // ohne Zeilenende am Schluss
    CHECK(SetIniValueInText("a=1\nb=2\n", L"b", L"3") == "a=1\nb=3\n");                              // LF bleibt LF
    CHECK(SetIniValueInText("", L"x", L"1") == "x=1\r\n");
    auto kv = ParseIni(SetIniValueInText(ini, L"param.tempo", L"2,5"));
    CHECK(kv[L"param.tempo"] == L"2,5" && kv[L"fit"] == L"cover");

    Gfx g;
    if (!g.Init(false)) {
        wprintf(L"SKIP  kein Direct3D 11\n");
        return;
    }
    // Aurora braucht ihre Eigenschaften (Tempo, Farben), sonst wird das Vorschaubild schwarz
    std::string aurora;
    for (auto& ex : Examples())
        if (std::wstring(ex.folder) == L"Aurora") aurora = ex.shader;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"lumen_selftest_thumb.hlsl";
    WriteTextFile(path, aurora);
    Wallpaper w;
    w.kind = WallKind::Shader;
    w.shaderPath = path;
    HBITMAP b = MakeWallpaperThumbnail(g, w, 96, 54);
    CHECK(b != nullptr);
    if (b) {
        BITMAP bm{};
        GetObject(b, sizeof(bm), &bm);
        const uint8_t* px = (const uint8_t*)bm.bmBits;
        std::set<uint32_t> colors;
        long sum = 0;
        for (int i = 0; i < bm.bmWidth * bm.bmHeight; i++) {
            colors.insert(*(const uint32_t*)(px + i * 4));
            sum += px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2];
        }
        CHECK(bm.bmWidth == 96 && bm.bmHeight == 54);
        CHECK(colors.size() > 200);                  // nicht einfarbig
        CHECK(sum / (bm.bmWidth * bm.bmHeight) > 5);  // und nicht schwarz
        DeleteObject(b);
    }
    DeleteFileW(path.c_str());
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
    TestPanorama();
    TestIniAndParams();
    TestSchedule();
    TestGif();
    TestGifPartialFrames();
    TestSettingsHelpers();
    wprintf(L"\n%ls (%d Fehler)\n", fails ? L"FEHLGESCHLAGEN" : L"ALLES OK", fails);
    return fails ? 1 : 0;
}
