#pragma once
#include "common.h"
#include <d3d11.h>
#include <dxgi1_3.h>

#include <mfobjects.h>


constexpr int kMaxParams = 16;   // einstellbare Werte pro Shader (muss zu lumenParams im Prelude passen)

// Werte, die jeder Shader bekommt (muss zum cbuffer im Prelude passen, 16-Byte-ausgerichtet)
struct ShaderConstants {
    float resolution[2];
    float time;
    float timeDelta;
    float mouse[4];      // xy Pixel, z Taste gedrückt, w Maus über Desktop auf diesem Bildschirm
    float audio[4];      // Bass, Mitten, Höhen, Pegel
    float bands[16];
    float click[4];      // xy Position, z Sekunden seit Klick, w Anzahl Klicks
    float channelRes[2];
    float hour;
    float frame;
    // fragCoord = Bildpunkt * map.zw + map.xy. Normal (0, 0, 1, 1); im Panorama liegt jeder Bildschirm an seiner
    // Stelle im gemeinsamen Desktop-Rechteck, unabhängig von seiner Renderauflösung.
    float map[4] = {0, 0, 1, 1};
    float params[kMaxParams][4] = {};   // Eigenschaften (siehe ShaderParam), Nummer = Reihenfolge im Shader
};
static_assert(sizeof(ShaderConstants) % 16 == 0, "cbuffer muss 16-Byte-ausgerichtet sein");

// ---- Quellen (Bild/Video), werden zwischen Bildschirmen geteilt -------------
class Gfx;

class Source {
public:
    virtual ~Source() = default;
    virtual ID3D11ShaderResourceView* Srv() = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual bool Animated() const { return false; }
    // Einmal pro Frame; active = mindestens ein Bildschirm zeigt die Quelle gerade
    virtual void Update(double /*now*/, bool /*active*/) {}
    virtual void SetSpeed(double) {}
    // Zeitpunkt (NowSeconds), zu dem sich der Inhalt das nächste Mal ändert; 0 = unbekannt/ständig. Erlaubt, zwischen
    // zwei Bildern nichts zu zeichnen (GIF mit 100 ms pro Bild braucht keine 30 Zeichenvorgänge pro Sekunde).
    virtual double NextChange() const { return 0; }
    uint64_t lastFrameId = 0;
};

std::shared_ptr<Source> LoadImageSource(Gfx& gfx, const std::wstring& path, std::wstring& err);
std::shared_ptr<Source> OpenVideoSource(Gfx& gfx, const std::wstring& path, std::wstring& err);

// ---- Eigenschaften: Werte, die man in wallpaper.ini einstellen kann ---------------------
// Ein Shader deklariert sie in Kommentaren und benutzt sie danach wie Variablen:
//   // @param tempo float 1.0 0.2 3.0 Geschwindigkeit     (Vorgabe, kleinster, größter Wert, Beschriftung)
//   // @param farbe color #19FF8C Hauptfarbe
// In wallpaper.ini steht der Wert dann als  param.tempo=2.0  bzw.  param.farbe=#FF8800
struct ShaderParam {
    std::wstring name, label;
    bool color = false;
    float def[3] = {0, 0, 0};   // Vorgabe: Zahl in [0], Farbe als rgb 0..1
    float mn = 0, mx = 1;       // Zahlenbereich
};
std::vector<ShaderParam> ParseShaderParams(const std::string& shaderSource);
// Aktueller Wert (aus ini, sonst Vorgabe; Zahlen auf mn..mx begrenzt). ini-Schlüssel sind kleingeschrieben.
void ParamValue(const ShaderParam& p, const std::map<std::wstring, std::wstring>& ini, float out[4]);

// ---- Kompilierte Shader --------------------------------------------------------
struct Program {
    ComPtr<ID3D11PixelShader> ps;
    bool usesAudio = false, usesMouse = false, usesTime = false;
};

// ---- Gerät ---------------------------------------------------------------------
class Gfx {
public:
    bool Init(bool allowVideo);
    void Shutdown();
    bool DeviceLost() const { return lost_; }
    void MarkLost() { lost_ = true; }

    ID3D11Device* Dev() const { return dev_.Get(); }
    ID3D11DeviceContext* Ctx() const { return ctx_.Get(); }
    IMFDXGIDeviceManager* MfManager();   // startet Media Foundation erst bei Bedarf

    bool CreateSwapChain(HWND hwnd, int w, int h, bool legacy, ComPtr<IDXGISwapChain1>& sc,
                         ComPtr<ID3D11RenderTargetView>& rtv);
    bool ResizeSwapChain(IDXGISwapChain1* sc, int w, int h, ComPtr<ID3D11RenderTargetView>& rtv);

    // Baut aus Nutzer-HLSL (mainImage) einen Pixel-Shader. Ergebnis wird im Speicher und auf Platte gecacht.
    std::shared_ptr<Program> GetProgram(const std::string& userSource, int fitMode, std::wstring& err);
    std::shared_ptr<Program> ErrorProgram();

    // Zeichnet einen Frame
    void Draw(ID3D11RenderTargetView* rtv, int w, int h, Program* prog, Source* src, const ShaderConstants& c);

    // NV12 (Y- und UV-Ebene) -> BGRA, für Videos ohne Farbraum-Wandlung im Decoder
    void ConvertNV12(ID3D11ShaderResourceView* y, ID3D11ShaderResourceView* uv, ID3D11RenderTargetView* out,
                     UINT w, UINT h, float uvScaleX, float uvScaleY, bool bt709);

    // Freigeben nicht mehr genutzter Ressourcen
    void Trim();

private:
    bool CompileHlsl(const std::string& src, const char* entry, const char* target, std::string& bytecode,
                     std::wstring& err);
    bool LoadCompiler();
    void UnloadCompiler();

    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> cbuf_;
    ComPtr<IMFDXGIDeviceManager> mfMgr_;
    ComPtr<ID3D11PixelShader> convPs_;
    ComPtr<ID3D11Buffer> convCb_;
    bool mfStarted_ = false;
    bool videoSupport_ = false;
    bool lost_ = false;
    D3D_FEATURE_LEVEL fl_ = D3D_FEATURE_LEVEL_11_0;

    HMODULE compiler_ = nullptr;
    void* d3dCompile_ = nullptr;

    std::map<uint64_t, std::weak_ptr<Program>> programs_;
    std::shared_ptr<Program> errorProgram_;
};

// Shader-Schnittstelle für Nutzer (wird vor jeden Shader gesetzt)
const char* ShaderPrelude();
std::string DefaultMediaShader();
