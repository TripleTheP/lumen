#include "settings.h"
#include "commands.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <wincodec.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <cmath>
#include <iterator>

// =====================================================================================
//  Vorschaubilder
// =====================================================================================
static HBITMAP NewDib(int w, int h, uint8_t** bits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // von oben nach unten
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* p = nullptr;
    HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
    *bits = (uint8_t*)p;
    return b;
}

static HBITMAP PlaceholderBitmap(int w, int h) {
    uint8_t* px = nullptr;
    HBITMAP b = NewDib(w, h, &px);
    if (!b) return nullptr;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        px[i * 4 + 0] = 0x38;
        px[i * 4 + 1] = 0x30;
        px[i * 4 + 2] = 0x2D;
        px[i * 4 + 3] = 0xFF;
    }
    return b;
}

static HBITMAP ThumbFromShader(Gfx& gfx, const Wallpaper& w, int tw, int th) {
    std::string code;
    if (!ReadTextFile(w.shaderPath, code)) return nullptr;
    std::wstring err;
    auto prog = gfx.GetProgram(code, w.fit, err);
    if (!prog) return nullptr;

    // Dreifach größer zeichnen und herunterrechnen: feine Muster (Punkte, Sterne) bleiben sonst unter einem Bildpunkt
    const int ss = 3, rw = tw * ss, rh = th * ss;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = rw;
    d.Height = rh;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> tex, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(gfx.Dev()->CreateTexture2D(&d, nullptr, &tex)) || FAILED(gfx.Dev()->CreateRenderTargetView(tex.Get(), nullptr, &rtv)))
        return nullptr;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(gfx.Dev()->CreateTexture2D(&d, nullptr, &staging))) return nullptr;

    // Ein Standbild, das typisch aussieht: etwas Musik, nichts angeklickt, Maus nicht da, aktuelle Uhrzeit
    ShaderConstants c{};
    c.resolution[0] = (float)rw;
    c.resolution[1] = (float)rh;
    c.time = 20.0f;
    c.audio[0] = 0.45f;
    c.audio[1] = 0.35f;
    c.audio[2] = 0.25f;
    c.audio[3] = 0.4f;
    for (int i = 0; i < 16; i++) c.bands[i] = 0.2f + 0.55f * (0.5f + 0.5f * std::sin(i * 0.8f + 1.0f));
    c.click[2] = 10000.0f;
    SYSTEMTIME lt;
    GetLocalTime(&lt);
    c.hour = lt.wHour + lt.wMinute / 60.0f;
    auto params = ParseShaderParams(code);   // ohne Werte wäre z. B. ein Tempo von 0 ein schwarzes Bild
    for (size_t i = 0; i < params.size(); i++) ParamValue(params[i], w.ini, c.params[i]);

    gfx.Draw(rtv.Get(), rw, rh, prog.get(), nullptr, c);
    gfx.Ctx()->CopyResource(staging.Get(), tex.Get());
    gfx.Ctx()->OMSetRenderTargets(0, nullptr, nullptr);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(gfx.Ctx()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return nullptr;
    uint8_t* px = nullptr;
    HBITMAP b = NewDib(tw, th, &px);
    if (b)
        for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++) {
                int sum[3] = {0, 0, 0};
                for (int j = 0; j < ss; j++) {
                    const uint8_t* row = (const uint8_t*)m.pData + (size_t)(y * ss + j) * m.RowPitch + (size_t)x * ss * 4;
                    for (int i = 0; i < ss; i++)
                        for (int k = 0; k < 3; k++) sum[k] += row[i * 4 + k];
                }
                uint8_t* o = px + ((size_t)y * tw + x) * 4;
                for (int k = 0; k < 3; k++) o[k] = (uint8_t)(sum[k] / (ss * ss));
                o[3] = 0xFF;
            }
    gfx.Ctx()->Unmap(staging.Get(), 0);
    return b;
}

static HBITMAP ThumbFromImage(const std::wstring& path, int tw, int th) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return nullptr;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &frame)))
        return nullptr;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h) return nullptr;
    double s = std::max((double)tw / w, (double)th / h);   // füllen, Rand abschneiden
    UINT sw = std::max<UINT>(tw, (UINT)std::ceil(w * s)), sh = std::max<UINT>(th, (UINT)std::ceil(h * s));
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame.Get(), sw, sh, WICBitmapInterpolationModeFant)) ||
        FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return nullptr;
    uint8_t* px = nullptr;
    HBITMAP b = NewDib(tw, th, &px);
    if (!b) return nullptr;
    WICRect crop{(INT)((sw - tw) / 2), (INT)((sh - th) / 2), tw, th};
    if (FAILED(conv->CopyPixels(&crop, tw * 4, (UINT)tw * th * 4, px))) {
        DeleteObject(b);
        return nullptr;
    }
    for (size_t i = 0; i < (size_t)tw * th; i++) px[i * 4 + 3] = 0xFF;   // durchsichtige Bereiche: schwarz
    return b;
}

// Vorschau der Windows-Shell (Videos, auch Dinge, die WIC nicht kennt)
static HBITMAP ThumbFromShell(const std::wstring& path, int tw, int th) {
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return nullptr;
    HBITMAP shell = nullptr;
    if (FAILED(factory->GetImage(SIZE{tw, th}, SIIGBF_BIGGERSIZEOK, &shell)) || !shell) return nullptr;
    BITMAP bm{};
    GetObject(shell, sizeof(bm), &bm);
    uint8_t* px = nullptr;
    HBITMAP out = NewDib(tw, th, &px);
    if (out && bm.bmWidth > 0 && bm.bmHeight > 0) {
        HDC screen = GetDC(nullptr);
        HDC src = CreateCompatibleDC(screen), dst = CreateCompatibleDC(screen);
        HGDIOBJ oldSrc = SelectObject(src, shell), oldDst = SelectObject(dst, out);
        double s = std::max((double)tw / bm.bmWidth, (double)th / bm.bmHeight);
        int cw = std::min<int>(bm.bmWidth, (int)std::lround(tw / s)), ch = std::min<int>(bm.bmHeight, (int)std::lround(th / s));
        SetStretchBltMode(dst, HALFTONE);
        SetBrushOrgEx(dst, 0, 0, nullptr);
        StretchBlt(dst, 0, 0, tw, th, src, (bm.bmWidth - cw) / 2, (bm.bmHeight - ch) / 2, cw, ch, SRCCOPY);
        SelectObject(src, oldSrc);
        SelectObject(dst, oldDst);
        DeleteDC(src);
        DeleteDC(dst);
        ReleaseDC(nullptr, screen);
        for (size_t i = 0; i < (size_t)tw * th; i++) px[i * 4 + 3] = 0xFF;
    }
    DeleteObject(shell);
    return out;
}

HBITMAP MakeWallpaperThumbnail(Gfx& gfx, const Wallpaper& w, int tw, int th) {
    HBITMAP b = nullptr;
    if (w.kind == WallKind::Shader) b = ThumbFromShader(gfx, w, tw, th);
    else if (w.kind == WallKind::Image) b = ThumbFromImage(w.source, tw, th);
    if (!b && w.kind != WallKind::Shader) b = ThumbFromShell(w.source, tw, th);
    return b;
}

// =====================================================================================
//  Fenster
// =====================================================================================
namespace {
enum : int {
    IDC_LIST = 50, IDC_SCREEN, IDC_FPS, IDC_SCALE,
    IDC_TABBTN = 60,    // + 0/1 (Reiter „Eigenschaften“ und „Einstellungen“)
    IDC_SLIDER = 200,   // + Nummer der Eigenschaft
    IDC_SWATCH = 300,
    IDC_CMD = 2000,     // + Menübefehl (Schalter und Knöpfe)
};
const UINT TIMER_THUMB = 1;
const wchar_t kClass[] = L"LumenSettings", kPageClass[] = L"LumenSettingsPage";

struct CheckDef { UINT cmd; const wchar_t* text; };
const CheckDef kChecks[] = {
    {ID_BATTERY, L"Im Akkubetrieb und Energiesparmodus pausieren"},
    {ID_FULLSCREEN, L"Bei Vollbild-Programmen alle Bildschirme pausieren"},
    {ID_AUDIO, L"Auf Musik reagieren"},
    {ID_SHADER_RES, L"Shader höchstens in 1080p berechnen (spart GPU)"},
    {ID_SPAN, L"Ein Hintergrund über alle Bildschirme (Panorama)"},
    {ID_SCHEDULE, L"Zeitplan: Hintergrund nach Tageszeit"},
    {ID_WE, L"Videos aus Wallpaper Engine anzeigen"},
    {ID_AUTOSTART, L"Mit Windows starten"},
    {ID_PAUSE, L"Alles pausieren"},
};
const CheckDef kButtons[] = {
    {ID_EDITSCHEDULE, L"Zeitplan bearbeiten…"},
    {ID_ADDFILE, L"Bild oder Video hinzufügen…"},
    {ID_NEWSHADER, L"Eigenen Shader erstellen…"},
    {ID_OPENLIB, L"Bibliothek öffnen"},
};
const int kFps[] = {15, 30, 60, 144};
const int kScales[] = {100, 75, 50};

const SettingsPalette kLight = {RGB(243, 243, 243), RGB(255, 255, 255), RGB(27, 27, 27), RGB(96, 96, 96),
                                RGB(0, 103, 192), RGB(218, 218, 218), RGB(251, 251, 251), RGB(235, 235, 235)};
const SettingsPalette kDark = {RGB(32, 32, 32), RGB(43, 43, 43), RGB(255, 255, 255), RGB(165, 165, 165),
                               RGB(96, 205, 255), RGB(66, 66, 66), RGB(52, 52, 52), RGB(64, 64, 64)};

// uxtheme/dwmapi werden dynamisch geladen: ältere Windows-Versionen kennen manches nicht, dann bleibt es beim hellen Aussehen
void SetTheme(HWND h, const wchar_t* name) {
    using Fn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static Fn fn = (Fn)(void*)GetProcAddress(LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "SetWindowTheme");
    if (fn && h) fn(h, name, nullptr);
}

void SetDwm(HWND h, DWORD attr, DWORD value) {
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static Fn fn = (Fn)(void*)GetProcAddress(LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "DwmSetWindowAttribute");
    if (fn) fn(h, attr, &value, sizeof(value));
}

// Eigene Knöpfe: Zustand in GWLP_USERDATA (Bit 0 = angehakt, Bit 1 = Maus darüber)
LRESULT CALLBACK HoverProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
    auto setHover = [&](bool on) {
        LONG_PTR s = GetWindowLongPtrW(h, GWLP_USERDATA);
        LONG_PTR n = on ? (s | 2) : (s & ~(LONG_PTR)2);
        if (n != s) {
            SetWindowLongPtrW(h, GWLP_USERDATA, n);
            InvalidateRect(h, nullptr, FALSE);
        }
    };
    switch (m) {
    case WM_MOUSEMOVE: {
        if (!(GetWindowLongPtrW(h, GWLP_USERDATA) & 2)) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
        }
        setHover(true);
        break;
    }
    case WM_MOUSELEAVE: setHover(false); break;
    case WM_NCDESTROY: RemoveWindowSubclass(h, HoverProc, id); break;
    }
    return DefSubclassProc(h, m, w, l);
}

std::wstring FormatFloat(float v) {
    wchar_t b[32];
    swprintf(b, 32, L"%.3f", v);
    std::wstring s = b;
    while (s.size() > 1 && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

std::wstring HexColor(const float v[4]) {
    wchar_t b[16];
    swprintf(b, 16, L"#%02X%02X%02X", (int)std::lround(v[0] * 255), (int)std::lround(v[1] * 255), (int)std::lround(v[2] * 255));
    return b;
}
}  // namespace

bool SystemUsesDarkMode() {
    DWORD v = 1, n = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &v, &n) != ERROR_SUCCESS)
        return false;
    return v == 0;
}

// uxtheme-Ordinale 135 (SetPreferredAppMode) und 136 (FlushMenuThemes): nicht dokumentiert, aber seit Windows 10 1903 stabil;
// fehlen sie, bleibt das Menü einfach hell
void InitDarkMode() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    auto setMode = (int(WINAPI*)(int))(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    if (setMode) setMode(1);   // AllowDark: folgt der Windows-Einstellung
    RefreshDarkMenus();
}

void RefreshDarkMenus() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    auto flush = (void(WINAPI*)())(void*)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (flush) flush();
}

SettingsWindow::~SettingsWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
    if (brBg_) DeleteObject(brBg_);
    if (brPanel_) DeleteObject(brPanel_);
}

// Farben, Titelleiste und Steuerelement-Themen nach der Windows-Einstellung (hell/dunkel)
void SettingsWindow::ApplyTheme() {
    dark_ = SystemUsesDarkMode();
    pal_ = dark_ ? kDark : kLight;
    if (brBg_) DeleteObject(brBg_);
    if (brPanel_) DeleteObject(brPanel_);
    brBg_ = CreateSolidBrush(pal_.bg);
    brPanel_ = CreateSolidBrush(pal_.panel);
    SetDwm(hwnd_, 20, dark_ ? 1 : 0);        // DWMWA_USE_IMMERSIVE_DARK_MODE
    SetDwm(hwnd_, 35, pal_.bg);              // DWMWA_CAPTION_COLOR (Windows 11): Titelleiste in der Fensterfarbe
    SetDwm(hwnd_, 36, pal_.text);            // DWMWA_TEXT_COLOR
    const wchar_t* bars = dark_ ? L"DarkMode_Explorer" : L"Explorer";
    const wchar_t* combo = dark_ ? L"DarkMode_CFD" : L"CFD";
    SetTheme(list_, dark_ ? L"DarkMode_ItemsView" : L"Explorer");
    SetTheme(pageProps_, bars);
    for (HWND c : {screenCombo_, fpsCombo_, scaleCombo_}) SetTheme(c, combo);
    ListView_SetBkColor(list_, pal_.panel);
    ListView_SetTextBkColor(list_, pal_.panel);
    ListView_SetTextColor(list_, pal_.text);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void SettingsWindow::SetChecked(HWND h, bool on) {
    LONG_PTR s = GetWindowLongPtrW(h, GWLP_USERDATA);
    LONG_PTR n = on ? (s | 1) : (s & ~(LONG_PTR)1);
    if (n != s) {
        SetWindowLongPtrW(h, GWLP_USERDATA, n);
        InvalidateRect(h, nullptr, FALSE);
    }
}

// Eigene Zeichnung für Schalter, Knöpfe, Reiter und Farbfelder (GDI, ohne zusätzliche Bibliotheken)
void SettingsWindow::DrawItem(DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    int id = (int)d->CtlID;
    bool disabled = d->itemState & ODS_DISABLED, pressed = d->itemState & ODS_SELECTED;
    bool focus = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);
    LONG_PTR st = GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA);
    bool hover = st & 2, checked = st & 1;
    HGDIOBJ oldFont = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    auto fill = [&](const RECT& r, COLORREF c) {
        SetDCBrushColor(dc, c);
        FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
    };
    auto box = [&](const RECT& r, int rad, COLORREF f, COLORREF line) {
        HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN)), ob = SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCPenColor(dc, line);
        SetDCBrushColor(dc, f);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
        SelectObject(dc, op);
        SelectObject(dc, ob);
    };
    auto text = [&](RECT r, UINT flags, COLORREF c) {
        wchar_t buf[256];
        GetWindowTextW(d->hwndItem, buf, 256);
        SetTextColor(dc, c);
        DrawTextW(dc, buf, -1, &r, flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    };

    if (id >= IDC_SWATCH && id < IDC_SWATCH + kMaxParams) {
        size_t i = (size_t)(id - IDC_SWATCH);
        fill(rc, pal_.panel);
        if (i < props_.size()) {
            COLORREF c = RGB((int)std::lround(props_[i].v[0] * 255), (int)std::lround(props_[i].v[1] * 255),
                             (int)std::lround(props_[i].v[2] * 255));
            RECT r = rc;
            InflateRect(&r, -1, -1);
            box(r, S(5), c, focus ? pal_.accent : (hover ? pal_.text : pal_.border));
        }
    } else if (id >= IDC_TABBTN && id < IDC_TABBTN + 2) {
        bool sel = tabSel_ == id - IDC_TABBTN;
        fill(rc, hover && !sel ? pal_.hover : pal_.bg);
        text(rc, DT_CENTER, sel ? pal_.text : pal_.dim);
        if (sel) {
            RECT bar{rc.left + S(14), rc.bottom - S(3), rc.right - S(14), rc.bottom};
            fill(bar, pal_.accent);
        }
        if (focus) {
            RECT f = rc;
            InflateRect(&f, -S(4), -S(4));
            SetDCBrushColor(dc, pal_.accent);
            FrameRect(dc, &f, (HBRUSH)GetStockObject(DC_BRUSH));
        }
    } else if (id >= IDC_CMD && std::any_of(checks_.begin(), checks_.end(), [&](auto& c) { return (int)(IDC_CMD + c.first) == id; })) {
        fill(rc, pal_.panel);
        int bs = S(18), by = rc.top + (rc.bottom - rc.top - bs) / 2;
        RECT b{rc.left + S(2), by, rc.left + S(2) + bs, by + bs};
        COLORREF edge = disabled ? pal_.border : (checked ? pal_.accent : (hover ? pal_.text : pal_.dim));
        box(b, S(4), checked && !disabled ? pal_.accent : pal_.panel, edge);
        if (checked) {
            HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), disabled ? pal_.dim : (dark_ ? RGB(0, 0, 0) : RGB(255, 255, 255)));
            HGDIOBJ op = SelectObject(dc, pen);
            POINT p[3] = {{b.left + bs * 22 / 100, b.top + bs * 52 / 100}, {b.left + bs * 42 / 100, b.top + bs * 72 / 100},
                          {b.left + bs * 78 / 100, b.top + bs * 30 / 100}};
            Polyline(dc, p, 3);
            SelectObject(dc, op);
            DeleteObject(pen);
        }
        RECT t{b.right + S(10), rc.top, rc.right, rc.bottom};
        text(t, DT_LEFT | DT_END_ELLIPSIS, disabled ? pal_.dim : pal_.text);
        if (focus) {
            RECT f = rc;
            DrawFocusRect(dc, &f);
        }
    } else {   // Knopf
        fill(rc, pal_.panel);
        RECT r = rc;
        InflateRect(&r, -1, -1);
        box(r, S(5), pressed ? pal_.border : (hover ? pal_.hover : pal_.ctl), focus ? pal_.accent : pal_.border);
        text(rc, DT_CENTER, disabled ? pal_.dim : pal_.text);
    }
    SelectObject(dc, oldFont);
}

// Regler im selben Stil: dünne Schiene, Teil links vom Griff in der Akzentfarbe, runder Griff
LRESULT SettingsWindow::DrawSlider(NMCUSTOMDRAW* cd) {
    HWND bar = cd->hdr.hwndFrom;
    HDC dc = cd->hdc;
    if (cd->dwDrawStage == CDDS_PREPAINT) {
        FillRect(dc, &cd->rc, brPanel_);
        return CDRF_NOTIFYITEMDRAW;
    }
    if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    RECT thumb;
    SendMessageW(bar, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
    int cx = (thumb.left + thumb.right) / 2, cy = (thumb.top + thumb.bottom) / 2;
    auto ellipse = [&](int r, COLORREF f, COLORREF line) {
        HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN)), ob = SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCPenColor(dc, line);
        SetDCBrushColor(dc, f);
        Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
    };
    if (cd->dwItemSpec == TBCD_CHANNEL) {
        FillRect(dc, &cd->rc, brPanel_);
        int h = std::max(3, S(4)), mid = (cd->rc.top + cd->rc.bottom) / 2;
        RECT all{cd->rc.left, mid - h / 2, cd->rc.right, mid - h / 2 + h}, on = all;
        on.right = std::min(on.right, (LONG)cx);
        SetDCBrushColor(dc, pal_.border);
        FillRect(dc, &all, (HBRUSH)GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, pal_.accent);
        FillRect(dc, &on, (HBRUSH)GetStockObject(DC_BRUSH));
        return CDRF_SKIPDEFAULT;
    }
    if (cd->dwItemSpec == TBCD_THUMB) {
        bool active = (cd->uItemState & CDIS_SELECTED) || (cd->uItemState & CDIS_FOCUS);
        FillRect(dc, &cd->rc, brPanel_);   // alles im Griff-Rechteck löschen, sonst bleiben Schlieren
        int rad = std::max(4, (int)std::min(thumb.right - thumb.left, thumb.bottom - thumb.top) / 2 - 1);
        ellipse(rad, pal_.ctl, pal_.border);
        ellipse(active ? rad * 6 / 10 : rad * 5 / 10, pal_.accent, pal_.accent);
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

void SettingsWindow::MakeFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    using Fn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    auto forDpi = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi");
    if (!forDpi || !forDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_)) {
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        ncm.lfMessageFont.lfHeight = -MulDiv(9, (int)dpi_, 72);
    }
    if (font_) DeleteObject(font_);
    font_ = CreateFontIndirectW(&ncm.lfMessageFont);
}

void SettingsWindow::ApplyFont() {
    EnumChildWindows(hwnd_, [](HWND h, LPARAM l) -> BOOL {
        SendMessageW(h, WM_SETFONT, (WPARAM)l, TRUE);
        return TRUE;
    }, (LPARAM)font_);
}

void SettingsWindow::Show() {
    if (hwnd_) {
        if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        Refresh(true);
        return;
    }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
        wc.hbrBackground = nullptr;   // Hintergrund zeichnen wir selbst (hell/dunkel)
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        wc.lpfnWndProc = PageProc;
        wc.hIcon = nullptr;
        wc.lpszClassName = kPageClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    // Auf dem Bildschirm öffnen, auf dem gerade die Maus ist (so stimmt auch gleich die DPI-Skalierung)
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, kClass, L"Lumen – Einstellungen",
                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, mi.rcWork.left + 40, mi.rcWork.top + 40, 300, 300, nullptr, nullptr,
                    inst, this);
    if (!hwnd_) {
        Log(L"Einstellungsfenster konnte nicht erstellt werden (Fehler %u)", (unsigned)GetLastError());
        return;
    }
    int w = std::min(S(1080), (int)(mi.rcWork.right - mi.rcWork.left) - S(40));
    int h = std::min(S(740), (int)(mi.rcWork.bottom - mi.rcWork.top) - S(40));
    SetWindowPos(hwnd_, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
                 mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2, w, h, SWP_NOZORDER);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK SettingsWindow::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SettingsWindow* self;
    if (m == WM_NCCREATE) {
        self = (SettingsWindow*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = h;
    } else {
        self = (SettingsWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, m, w, l);
    return self->OnMessage(h, m, w, l);
}

// Die Seiten reichen alles an das Hauptfenster weiter; die Eigenschaften-Seite scrollt selbst
LRESULT CALLBACK SettingsWindow::PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SettingsWindow* self;
    if (m == WM_NCCREATE) {
        self = (SettingsWindow*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (SettingsWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    switch (m) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
        return SendMessageW(GetParent(h), m, w, l);
    case WM_HSCROLL:
        return SendMessageW(GetParent(h), m, w, l);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (!self || !self->brPanel_) break;
        SetBkMode((HDC)w, TRANSPARENT);
        SetTextColor((HDC)w, self->pal_.text);
        return (LRESULT)self->brPanel_;
    case WM_ERASEBKGND:
        if (!self || !self->brPanel_) break;
        {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect((HDC)w, &rc, self->brPanel_);
        }
        return 1;
    case WM_VSCROLL:
        if (self) self->OnScroll(h, w);
        return 0;
    case WM_MOUSEWHEEL:
        if (self && h == self->pageProps_) {
            self->ScrollTo(self->scrollPos_ - GET_WHEEL_DELTA_WPARAM(w) * self->S(40) / WHEEL_DELTA);
            return 0;
        }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

void SettingsWindow::CreateControls() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, HWND parent, DWORD ex = 0) {
        return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, inst, nullptr);
    };
    screenLabel_ = child(L"STATIC", L"Anzeigen auf:", SS_LEFT, 0, hwnd_);
    screenCombo_ = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_SCREEN, hwnd_);
    list_ = child(WC_LISTVIEWW, L"", LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_AUTOARRANGE | WS_TABSTOP, IDC_LIST, hwnd_);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_DOUBLEBUFFER);
    // Eigene Reiter statt des Tab-Steuerelements (das lässt sich nicht dunkel färben)
    tabBtn_[0] = child(L"BUTTON", L"Eigenschaften", BS_OWNERDRAW | WS_TABSTOP, IDC_TABBTN, hwnd_);
    tabBtn_[1] = child(L"BUTTON", L"Einstellungen", BS_OWNERDRAW | WS_TABSTOP, IDC_TABBTN + 1, hwnd_);

    auto page = [&]() {
        return CreateWindowExW(WS_EX_CONTROLPARENT, kPageClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 10, 10,
                               hwnd_, nullptr, inst, this);
    };
    pageProps_ = page();
    pageSettings_ = page();
    propsTitle_ = child(L"STATIC", L"", SS_LEFT, 0, pageProps_);

    for (auto& c : kChecks)
        checks_.push_back({c.cmd, child(L"BUTTON", c.text, BS_OWNERDRAW | WS_TABSTOP, IDC_CMD + c.cmd, pageSettings_)});
    fpsLabel_ = child(L"STATIC", L"Bildrate:", SS_LEFT, 0, pageSettings_);
    fpsCombo_ = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, IDC_FPS, pageSettings_);
    for (int f : kFps) ComboBox_AddString(fpsCombo_, (std::to_wstring(f) + L" Bilder pro Sekunde").c_str());
    scaleLabel_ = child(L"STATIC", L"Renderauflösung:", SS_LEFT, 0, pageSettings_);
    scaleCombo_ = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, IDC_SCALE, pageSettings_);
    for (int s : kScales) ComboBox_AddString(scaleCombo_, (std::to_wstring(s) + L" %").c_str());
    for (auto& b : kButtons) buttons_.push_back({b.cmd, child(L"BUTTON", b.text, BS_OWNERDRAW | WS_TABSTOP, IDC_CMD + b.cmd, pageSettings_)});
    for (HWND h : {tabBtn_[0], tabBtn_[1]}) SetWindowSubclass(h, HoverProc, 1, 0);
    for (auto& c : checks_) SetWindowSubclass(c.second, HoverProc, 1, 0);
    for (auto& b : buttons_) SetWindowSubclass(b.second, HoverProc, 1, 0);
    ApplyFont();
    ApplyTheme();
}

void SettingsWindow::Layout() {
    if (!hwnd_ || !tabBtn_[0]) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int W = rc.right, H = rc.bottom, m = S(16), rw = S(370);
    int top = comboShown_ ? S(38) : 0;
    MoveWindow(screenLabel_, m, m + S(5), S(110), S(22), TRUE);
    MoveWindow(screenCombo_, m + S(112), m, S(260), S(300), TRUE);
    int lw = std::max(S(200), W - 3 * m - rw), lh = std::max(S(100), H - 2 * m - top);
    MoveWindow(list_, m, m + top, lw, lh, TRUE);
    listFrame_ = {m - 1, m + top - 1, m + lw + 1, m + top + lh + 1};
    int tx = W - m - rw, ty = m, th = H - 2 * m, tabH = S(38);
    MoveWindow(tabBtn_[0], tx, ty, rw / 2, tabH, TRUE);
    MoveWindow(tabBtn_[1], tx + rw / 2, ty, rw - rw / 2, tabH, TRUE);
    int py = ty + tabH + S(4), ph = std::max(S(100), th - tabH - S(4));
    for (HWND p : {pageProps_, pageSettings_}) SetWindowPos(p, HWND_TOP, tx, py, rw, ph, SWP_NOACTIVATE);
    pageFrame_ = {tx - 1, py - 1, tx + rw + 1, py + ph + 1};
    LayoutSettingsPage();
    LayoutPropsPage();
    UpdateScroll();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::LayoutSettingsPage() {
    RECT rc;
    GetClientRect(pageSettings_, &rc);
    int x = S(10), w = rc.right - 2 * x, y = S(10);
    for (auto& c : checks_) {
        MoveWindow(c.second, x, y, w, S(22), TRUE);
        y += S(30);
    }
    y += S(8);
    MoveWindow(fpsLabel_, x, y + S(4), S(120), S(22), TRUE);
    MoveWindow(fpsCombo_, x + S(130), y, w - S(130), S(200), TRUE);
    y += S(32);
    MoveWindow(scaleLabel_, x, y + S(4), S(120), S(22), TRUE);
    MoveWindow(scaleCombo_, x + S(130), y, w - S(130), S(200), TRUE);
    y += S(44);
    for (auto& b : buttons_) {
        MoveWindow(b.second, x, y, w, S(30), TRUE);
        y += S(36);
    }
}

void SettingsWindow::LayoutPropsPage() {
    if (!pageProps_) return;
    RECT rc;
    GetClientRect(pageProps_, &rc);
    int x = S(10), w = rc.right - 2 * x, y = S(10) - scrollPos_;
    MoveWindow(propsTitle_, x, y, w, S(44), TRUE);
    y += S(52);
    for (size_t i = 0; i < props_.size(); i++) {
        MoveWindow(propValueLabels_[i], x, y, w, S(20), TRUE);
        if (props_[i].p.color) MoveWindow(propSwatches_[i], x, y + S(24), S(90), S(26), TRUE);
        else MoveWindow(propSliders_[i], x, y + S(22), w, S(30), TRUE);
        y += S(62);
    }
    contentH_ = y + scrollPos_ + S(10);
}

void SettingsWindow::UpdateScroll() {
    if (!pageProps_) return;
    RECT rc;
    GetClientRect(pageProps_, &rc);
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, contentH_ - 1), (UINT)rc.bottom, scrollPos_};
    SetScrollInfo(pageProps_, SB_VERT, &si, TRUE);
}

void SettingsWindow::ScrollTo(int pos) {
    RECT rc;
    GetClientRect(pageProps_, &rc);
    pos = std::clamp(pos, 0, std::max(0, contentH_ - (int)rc.bottom));
    if (pos == scrollPos_) return;
    scrollPos_ = pos;
    LayoutPropsPage();
    UpdateScroll();
}

void SettingsWindow::OnScroll(HWND bar, WPARAM wp) {
    SCROLLINFO si{sizeof(si), SIF_ALL};
    GetScrollInfo(bar, SB_VERT, &si);
    int pos = scrollPos_;
    switch (LOWORD(wp)) {
    case SB_LINEUP: pos -= S(30); break;
    case SB_LINEDOWN: pos += S(30); break;
    case SB_PAGEUP: pos -= (int)si.nPage; break;
    case SB_PAGEDOWN: pos += (int)si.nPage; break;
    case SB_THUMBTRACK: pos = si.nTrackPos; break;
    }
    ScrollTo(pos);
}

void SettingsWindow::ShowTab(int i) {
    tabSel_ = i;
    InvalidateRect(tabBtn_[0], nullptr, FALSE);
    InvalidateRect(tabBtn_[1], nullptr, FALSE);
    ShowWindow(pageProps_, i == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(pageSettings_, i == 1 ? SW_SHOW : SW_HIDE);
}

int SettingsWindow::Target() const {
    if (!comboShown_) return -1;
    return ComboBox_GetCurSel(screenCombo_) - 1;   // 0 = alle
}

std::wstring SettingsWindow::CurrentId() const {
    int t = Target();
    if (t >= 0) return t < (int)screens_.size() ? screens_[t].wallId : L"";
    if (screens_.empty()) return L"";
    for (auto& s : screens_)
        if (s.wallId != screens_[0].wallId) return L"";
    return screens_[0].wallId;
}

// ---- Raster mit Vorschaubildern ---------------------------------------------------------
void SettingsWindow::RebuildList() {
    thumbW_ = S(192);
    thumbH_ = S(108);
    ListView_DeleteAllItems(list_);
    ListView_RemoveAllGroups(list_);
    HIMAGELIST il = ImageList_Create(thumbW_, thumbH_, ILC_COLOR32, (int)items_.size(), 16);
    if (HBITMAP ph = PlaceholderBitmap(thumbW_, thumbH_)) {
        for (size_t i = 0; i < items_.size(); i++) ImageList_Add(il, ph, nullptr);
        DeleteObject(ph);
    }
    if (HIMAGELIST old = ListView_SetImageList(list_, il, LVSIL_NORMAL)) ImageList_Destroy(old);
    ListView_SetIconSpacing(list_, thumbW_ + S(24), thumbH_ + S(52));
    ListView_EnableGroupView(list_, TRUE);

    std::map<std::wstring, int> groupIds;
    for (size_t i = 0; i < items_.size(); i++) {
        std::wstring g = items_[i].group.empty() ? L"Allgemein" : items_[i].group;
        if (!groupIds.count(g)) {
            int id = (int)groupIds.size();
            groupIds[g] = id;
            LVGROUP lg{};
            lg.cbSize = sizeof(lg);
            lg.mask = LVGF_HEADER | LVGF_GROUPID;
            lg.pszHeader = (LPWSTR)g.c_str();
            lg.iGroupId = id;
            ListView_InsertGroup(list_, -1, &lg);
        }
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_GROUPID | LVIF_PARAM;
        it.iItem = (int)i;
        it.iImage = (int)i;
        it.iGroupId = groupIds[g];
        it.lParam = (LPARAM)i;
        it.pszText = (LPWSTR)items_[i].name.c_str();
        ListView_InsertItem(list_, &it);
    }
    nextThumb_ = 0;
    SetTimer(hwnd_, TIMER_THUMB, 20, nullptr);
}

// Pro Zeitschritt ein Vorschaubild, damit das Fenster bedienbar bleibt (Wallpaper-Engine-Listen können lang sein)
void SettingsWindow::MakeNextThumbnail() {
    if (nextThumb_ >= items_.size() || !ctx_.gfx) {
        KillTimer(hwnd_, TIMER_THUMB);
        return;
    }
    size_t i = nextThumb_++;
    if (HBITMAP b = MakeWallpaperThumbnail(*ctx_.gfx, items_[i], thumbW_, thumbH_)) {
        if (HIMAGELIST il = ListView_GetImageList(list_, LVSIL_NORMAL)) ImageList_Replace(il, (int)i, b, nullptr);
        DeleteObject(b);
        ListView_RedrawItems(list_, (int)i, (int)i);
    }
}

void SettingsWindow::ApplyItem(int index) {
    if (index < 0 || index >= (int)items_.size() || items_[index].id == currentId_) return;
    ctx_.select(Target(), items_[index].id);
    Refresh(false);
}

// ---- Eigenschaften ------------------------------------------------------------------------
void SettingsWindow::RebuildProps() {
    std::wstring name;
    auto props = ctx_.props(name);
    std::wstring key = name + L"|";
    for (auto& p : props) key += p.p.name + L"=" + FormatFloat(p.v[0]) + HexColor(p.v) + L";";
    if (key == propsKey_) return;
    propsKey_ = key;
    for (HWND h : propCtrls_) DestroyWindow(h);
    propCtrls_.clear();
    propValueLabels_.clear();
    propSliders_.clear();
    propSwatches_.clear();
    props_ = props;
    scrollPos_ = 0;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    SetWindowTextW(propsTitle_, props_.empty() ? (name.empty() ? L"Wähle einen Hintergrund." :
                                                  L"Dieser Hintergrund hat keine einstellbaren Werte. Shader können welche mit „// @param“ anbieten (siehe README).")
                                                : (L"Eigenschaften von „" + name + L"“").c_str());
    for (size_t i = 0; i < props_.size(); i++) {
        auto& p = props_[i].p;
        std::wstring label = p.label.empty() ? p.name : p.label;
        HWND lbl = CreateWindowExW(0, L"STATIC", p.color ? label.c_str() : (label + L":  " + FormatFloat(props_[i].v[0])).c_str(),
                                   WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, pageProps_, nullptr, inst, nullptr);
        propValueLabels_.push_back(lbl);
        propCtrls_.push_back(lbl);
        HWND slider = nullptr, swatch = nullptr;
        if (p.color) {
            swatch = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, pageProps_,
                                     (HMENU)(INT_PTR)(IDC_SWATCH + i), inst, nullptr);
            propCtrls_.push_back(swatch);
        } else {
            slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH, 0, 0, 10, 10,
                                     pageProps_, (HMENU)(INT_PTR)(IDC_SLIDER + i), inst, nullptr);
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
            SendMessageW(slider, TBM_SETTHUMBLENGTH, S(22), 0);
            SendMessageW(slider, TBM_SETPOS, TRUE, (LPARAM)std::lround((props_[i].v[0] - p.mn) / (p.mx - p.mn) * 1000));
            propCtrls_.push_back(slider);
        }
        propSliders_.push_back(slider);
        propSwatches_.push_back(swatch);
    }
    for (HWND h : propCtrls_) SendMessageW(h, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(propsTitle_, WM_SETFONT, (WPARAM)font_, TRUE);
    LayoutPropsPage();
    UpdateScroll();
}

void SettingsWindow::OnPropColor(int i) {
    static COLORREF custom[16];
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd_;
    cc.rgbResult = RGB((int)std::lround(props_[i].v[0] * 255), (int)std::lround(props_[i].v[1] * 255), (int)std::lround(props_[i].v[2] * 255));
    cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return;
    props_[i].v[0] = GetRValue(cc.rgbResult) / 255.0f;
    props_[i].v[1] = GetGValue(cc.rgbResult) / 255.0f;
    props_[i].v[2] = GetBValue(cc.rgbResult) / 255.0f;
    InvalidateRect(propSwatches_[i], nullptr, TRUE);
    ctx_.setProp(props_[i].p.name, HexColor(props_[i].v), true);
}

// ---- Zustand lesen -------------------------------------------------------------------------
void SettingsWindow::Refresh(bool rescan) {
    if (!hwnd_) return;
    screens_ = ctx_.screens();
    bool showCombo = screens_.size() > 1 && !ctx_.checked(ID_SPAN);
    bool wasShown = comboShown_;
    comboShown_ = showCombo;
    if (ComboBox_GetCount(screenCombo_) != (int)screens_.size() + 1) {
        int keep = std::max(0, ComboBox_GetCurSel(screenCombo_));
        ComboBox_ResetContent(screenCombo_);
        ComboBox_AddString(screenCombo_, L"Alle Bildschirme");
        for (auto& s : screens_) ComboBox_AddString(screenCombo_, s.label.c_str());
        ComboBox_SetCurSel(screenCombo_, keep < (int)screens_.size() + 1 ? keep : 0);
    }
    ShowWindow(screenLabel_, showCombo ? SW_SHOW : SW_HIDE);
    ShowWindow(screenCombo_, showCombo ? SW_SHOW : SW_HIDE);
    if (showCombo != wasShown) Layout();

    for (auto& c : checks_) SetChecked(c.second, ctx_.checked(c.first));
    for (auto& c : checks_)
        if (c.first == ID_SPAN) EnableWindow(c.second, screens_.size() > 1 || ctx_.checked(ID_SPAN));
    int fps = ctx_.cfg->Fps(), scale = (int)std::lround(ctx_.cfg->RenderScale() * 100);
    for (size_t i = 0; i < std::size(kFps); i++)
        if (kFps[i] == fps) ComboBox_SetCurSel(fpsCombo_, (int)i);
    ComboBox_SetCurSel(scaleCombo_, -1);
    for (size_t i = 0; i < std::size(kScales); i++)
        if (kScales[i] == scale) ComboBox_SetCurSel(scaleCombo_, (int)i);

    if (rescan) {
        auto fresh = ctx_.items();
        bool same = fresh.size() == items_.size();
        for (size_t i = 0; same && i < fresh.size(); i++) same = fresh[i].id == items_[i].id && fresh[i].name == items_[i].name;
        if (!same || ListView_GetItemCount(list_) == 0) {
            items_ = std::move(fresh);
            RebuildList();
        }
    }
    currentId_ = CurrentId();
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (size_t i = 0; i < items_.size(); i++)
        if (ToLower(items_[i].id) == ToLower(currentId_)) {
            ListView_SetItemState(list_, (int)i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list_, (int)i, FALSE);
        }
    bool first = propsKey_.empty();
    RebuildProps();
    if (first) ShowTab(props_.empty() ? 1 : 0);
}

// ---- Nachrichten --------------------------------------------------------------------------------
LRESULT SettingsWindow::OnMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        dpi_ = GetDpiForWindow(hwnd_);
        MakeFont();
        CreateControls();
        Layout();
        Refresh(true);
        return 0;
    case WM_SIZE:
        Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize = {S(860), S(560)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w);
        auto* r = (RECT*)l;
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        MakeFont();
        ApplyFont();
        SendMessageW(propsTitle_, WM_SETFONT, (WPARAM)font_, TRUE);
        RebuildList();   // Vorschaubilder haben eine feste Pixelgröße
        Layout();
        Refresh(false);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE) Refresh(false);   // Änderungen über das Tray-Menü oder den Zeitplan übernehmen
        return 0;
    case WM_TIMER:
        if (w == TIMER_THUMB) MakeNextThumbnail();
        return 0;
    case WM_CTLCOLORSTATIC:   // Beschriftungen ohne grauen Kasten
        if (!brBg_) break;
        SetBkMode((HDC)w, TRANSPARENT);
        SetTextColor((HDC)w, pal_.text);
        return (LRESULT)brBg_;
    case WM_ERASEBKGND: {
        if (!brBg_) break;
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, brBg_);
        return 1;
    }
    case WM_PAINT: {   // dünne Rahmen um das Raster und die Seiten
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (brBg_) {
            SetDCBrushColor(dc, pal_.border);
            for (const RECT* r : {&listFrame_, &pageFrame_}) FrameRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH));
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETTINGCHANGE:   // Windows wechselt zwischen hell und dunkel
        if (l && lstrcmpW((LPCWSTR)l, L"ImmersiveColorSet") == 0 && tabBtn_[0]) {
            RefreshDarkMenus();
            ApplyTheme();
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == IDCANCEL) { Close(); return 0; }
        if (id >= IDC_TABBTN && id < IDC_TABBTN + 2 && code == BN_CLICKED) {
            ShowTab(id - IDC_TABBTN);
        } else if (id >= IDC_CMD && code == BN_CLICKED) {
            ctx_.command((UINT)(id - IDC_CMD));
            Refresh(true);
        } else if (id == IDC_FPS && code == CBN_SELCHANGE) {
            int i = ComboBox_GetCurSel(fpsCombo_);
            if (i >= 0) ctx_.command(ID_FPS_BASE + kFps[i]);
        } else if (id == IDC_SCALE && code == CBN_SELCHANGE) {
            int i = ComboBox_GetCurSel(scaleCombo_);
            if (i >= 0) ctx_.command(ID_SCALE_BASE + kScales[i]);
        } else if (id == IDC_SCREEN && code == CBN_SELCHANGE) {
            Refresh(false);
        } else if (id >= IDC_SWATCH && id < IDC_SWATCH + kMaxParams && code == BN_CLICKED) {
            OnPropColor(id - IDC_SWATCH);
        }
        return 0;
    }
    case WM_NOTIFY: {
        auto* nh = (NMHDR*)l;
        if (nh->code == NM_CUSTOMDRAW && nh->idFrom >= IDC_SLIDER && nh->idFrom < IDC_SLIDER + kMaxParams)
            return DrawSlider((NMCUSTOMDRAW*)l);
        if (nh->idFrom == IDC_LIST && (nh->code == NM_CLICK || nh->code == LVN_ITEMACTIVATE)) {
            auto* a = (NMITEMACTIVATE*)l;
            if (a->iItem >= 0) {
                LVITEMW it{};
                it.mask = LVIF_PARAM;
                it.iItem = a->iItem;
                if (ListView_GetItem(list_, &it)) ApplyItem((int)it.lParam);
            }
        }
        return 0;
    }
    case WM_HSCROLL: {
        HWND bar = (HWND)l;
        int i = GetDlgCtrlID(bar) - IDC_SLIDER;
        if (i < 0 || i >= (int)props_.size()) return 0;
        auto& p = props_[i].p;
        int code = LOWORD(w);
        float v = p.mn + (float)SendMessageW(bar, TBM_GETPOS, 0, 0) / 1000.0f * (p.mx - p.mn);
        props_[i].v[0] = v;
        SetWindowTextW(propValueLabels_[i], ((p.label.empty() ? p.name : p.label) + L":  " + FormatFloat(v)).c_str());
        // Beim Ziehen nur anzeigen (sofort, ohne Neuladen); erst beim Loslassen in die ini schreiben
        ctx_.setProp(p.name, FormatFloat(v), code != TB_THUMBTRACK);
        return 0;
    }
    case WM_DRAWITEM: {
        DrawItem((DRAWITEMSTRUCT*)l);
        return TRUE;
    }
    case WM_CLOSE:
        Close();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, TIMER_THUMB);
        hwnd_ = nullptr;
        tabBtn_[0] = tabBtn_[1] = list_ = nullptr;
        if (ctx_.closed) ctx_.closed();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
