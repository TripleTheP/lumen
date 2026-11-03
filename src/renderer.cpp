#include "renderer.h"
#include <d3dcompiler.h>
#include <d3d10.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <algorithm>

// ---- Media Foundation dynamisch laden ------------------------------------------------
// Windows-"N"-Editionen haben ohne Media Feature Pack kein MFPlat.dll. Lumen soll dort
// trotzdem starten (Shader und Bilder gehen dann, nur Videos nicht).
namespace mf {
using PStartup = HRESULT(WINAPI*)(ULONG, DWORD);
using PCreateDevMgr = HRESULT(WINAPI*)(UINT*, IMFDXGIDeviceManager**);
using PCreateAttributes = HRESULT(WINAPI*)(IMFAttributes**, UINT32);
using PCreateMediaType = HRESULT(WINAPI*)(IMFMediaType**);
using PCreateReader = HRESULT(WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
static PStartup Startup;
static PCreateDevMgr CreateDXGIDeviceManager;
static PCreateAttributes CreateAttributes;
static PCreateMediaType CreateMediaType;
static PCreateReader CreateSourceReaderFromURL;

static bool Load() {
    static int state = 0;   // 0 unversucht, 1 ok, 2 fehlt
    if (state) return state == 1;
    state = 2;
    HMODULE plat = LoadLibraryW(L"mfplat.dll");
    HMODULE rw = LoadLibraryW(L"mfreadwrite.dll");
    if (!plat || !rw) {
        Log(L"Media Foundation fehlt (Windows N ohne Media Feature Pack?) – Videos deaktiviert");
        return false;
    }
    Startup = (PStartup)(void*)GetProcAddress(plat, "MFStartup");
    CreateDXGIDeviceManager = (PCreateDevMgr)(void*)GetProcAddress(plat, "MFCreateDXGIDeviceManager");
    CreateAttributes = (PCreateAttributes)(void*)GetProcAddress(plat, "MFCreateAttributes");
    CreateMediaType = (PCreateMediaType)(void*)GetProcAddress(plat, "MFCreateMediaType");
    CreateSourceReaderFromURL = (PCreateReader)(void*)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
    if (!Startup || !CreateDXGIDeviceManager || !CreateAttributes || !CreateMediaType || !CreateSourceReaderFromURL)
        return false;
    state = 1;
    return true;
}
}  // namespace mf

// =====================================================================================
//  Shader-Schnittstelle
// =====================================================================================
static const char* kPrelude = R"HLSL(
// ---- Lumen Shader-Schnittstelle --------------------------------------------------
cbuffer LumenConstants : register(b0) {
    float2 iResolution;        // Größe in Pixeln
    float  iTime;              // Sekunden seit Start, beginnt jede Stunde wieder bei 0
    float  iTimeDelta;         // Sekunden seit letztem Bild
    float4 iMouse;             // xy Mausposition (Pixel, oben links = 0,0), z Taste gedrückt, w Maus über Desktop
    float4 iAudio;             // x Bass, y Mitten, z Höhen, w Pegel (0..1)
    float4 iBandsPacked[4];    // 16 Frequenzbänder -> band(i)
    float4 iClick;             // xy Position letzter Klick, z Sekunden seit Klick, w Anzahl Klicks
    float2 iChannelResolution; // Größe von iChannel0 (Bild/Video)
    float  iHour;              // lokale Uhrzeit 0..24 (z. B. 18.5 = 18:30)
    float  iFrame;
};
Texture2D    iChannel0 : register(t0);
SamplerState iSampler  : register(s0);

float band(int i) {
    i = clamp(i, 0, 15);
    float4 v = iBandsPacked[i / 4];
    int j = i % 4;
    return j == 0 ? v.x : (j == 1 ? v.y : (j == 2 ? v.z : v.w));
}
float bandf(float x) {
    float f = saturate(x) * 15.0;
    int i = (int)floor(f);
    return lerp(band(i), band(min(i + 1, 15)), frac(f));
}
float2 uvCover(float2 fragCoord) {
    float2 t = max(iChannelResolution, 1.0);
    float s = max(iResolution.x / t.x, iResolution.y / t.y);
    float2 size = t * s;
    return (fragCoord - (iResolution - size) * 0.5) / size;
}
float2 uvContain(float2 fragCoord) {
    float2 t = max(iChannelResolution, 1.0);
    float s = min(iResolution.x / t.x, iResolution.y / t.y);
    float2 size = t * s;
    return (fragCoord - (iResolution - size) * 0.5) / size;
}
#ifndef LUMEN_FIT
#define LUMEN_FIT 0
#endif
float4 texFit(float2 fragCoord) {
#if LUMEN_FIT == 1
    float2 uv = uvContain(fragCoord);
    if (any(uv < 0.0) || any(uv > 1.0)) return float4(0, 0, 0, 1);
    return iChannel0.SampleLevel(iSampler, uv, 0);
#elif LUMEN_FIT == 2
    return iChannel0.SampleLevel(iSampler, fragCoord / iResolution, 0);
#else
    return iChannel0.SampleLevel(iSampler, uvCover(fragCoord), 0);
#endif
}
float L_hash(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
float L_noise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float a = L_hash(i), b = L_hash(i + float2(1, 0)), c = L_hash(i + float2(0, 1)), d = L_hash(i + float2(1, 1));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float L_fbm(float2 p) {
    float v = 0.0, a = 0.5;
    [unroll] for (int i = 0; i < 5; i++) { v += a * L_noise(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return v;
}
float3 L_hue(float h) {
    return saturate(abs(frac(h + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
}
)HLSL";

static const char* kEpilogue = R"HLSL(
float4 PSMain(float4 lumenPos : SV_Position) : SV_Target {
    float4 c = mainImage(lumenPos.xy);
    return float4(saturate(c.rgb), 1.0);
}
)HLSL";

static const char* kVertexShader = R"HLSL(
float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
)HLSL";

static const char* kNv12Shader = R"HLSL(
Texture2D PlaneY : register(t0);
Texture2D PlaneUV : register(t1);
SamplerState S : register(s0);
cbuffer Conv : register(b0) { float2 invSize; float2 uvScale; float bt709; float3 pad; };
float4 PSConv(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy * invSize * uvScale;
    float  y = (PlaneY.SampleLevel(S, uv, 0).r - 16.0 / 255.0) * (255.0 / 219.0);
    float2 c = (PlaneUV.SampleLevel(S, uv, 0).rg - 128.0 / 255.0) * (255.0 / 224.0);
    float3 rgb = bt709 > 0.5
        ? float3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y, y + 1.8556 * c.x)
        : float3(y + 1.402 * c.y, y - 0.344136 * c.x - 0.714136 * c.y, y + 1.772 * c.x);
    return float4(saturate(rgb), 1.0);
}
)HLSL";

static const char* kErrorShader = R"HLSL(
float4 mainImage(float2 p) {
    float s = step(0.5, frac((p.x + p.y) / 90.0));
    return float4(lerp(float3(0.10, 0.02, 0.05), float3(0.18, 0.03, 0.07), s), 1);
}
)HLSL";

const char* ShaderPrelude() { return kPrelude; }

std::string DefaultMediaShader() {
    return "float4 mainImage(float2 fragCoord) { return texFit(fragCoord); }\n";
}

static std::wstring CacheDir() {
    static std::wstring d;
    if (d.empty()) {
        d = JoinPath(DirOf(LibraryDir()), L"Cache");
        EnsureDir(d);
    }
    return d;
}

// =====================================================================================
//  Gerät
// =====================================================================================
bool Gfx::Init(bool allowVideo) {
    lost_ = false;
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                               D3D_FEATURE_LEVEL_10_0};
    UINT base = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
    videoSupport_ = false;
    if (allowVideo) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, base | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                               levels, 4, D3D11_SDK_VERSION, &dev_, &fl_, &ctx_);
        if (FAILED(hr))   // Manche Treiber kennen 11_1 nicht
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, base | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                   levels + 1, 3, D3D11_SDK_VERSION, &dev_, &fl_, &ctx_);
        videoSupport_ = SUCCEEDED(hr);
    }
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, base, levels + 1, 3, D3D11_SDK_VERSION,
                               &dev_, &fl_, &ctx_);
    if (FAILED(hr)) {
        Log(L"D3D11: Hardware-Gerät fehlgeschlagen (0x%08X), nutze WARP", (unsigned)hr);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, base, levels + 1, 3, D3D11_SDK_VERSION, &dev_,
                               &fl_, &ctx_);
    }
    if (FAILED(hr)) {
        Log(L"D3D11: kein Gerät (0x%08X)", (unsigned)hr);
        return false;
    }
    Log(L"D3D11: Feature-Level 0x%X, Video %s", (unsigned)fl_, videoSupport_ ? L"ja" : L"nein");

    // Media Foundation greift aus eigenen Threads auf das Gerät zu
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(dev_.As(&mt))) mt->SetMultithreadProtected(TRUE);

    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    dev_.As(&dxgiDev);
    dxgiDev->GetAdapter(&adapter);
    dxgiDev->SetGPUThreadPriority(-4);   // Spiele haben Vorrang
    adapter->GetParent(IID_PPV_ARGS(&factory_));
    if (!factory_) return false;

    // Vertex-Shader (Vollbild-Dreieck)
    const char* vsTarget = fl_ >= D3D_FEATURE_LEVEL_11_0 ? "vs_5_0" : "vs_4_0";
    std::string vsCode;
    std::wstring err;
    std::string vsKey = std::string(kVertexShader) + vsTarget;
    std::wstring vsCache = JoinPath(CacheDir(), L"vs_" + std::to_wstring(Fnv1a(vsKey)) + L".cso");
    if (!ReadTextFile(vsCache, vsCode) || vsCode.empty()) {
        if (!CompileHlsl(kVertexShader, "VSMain", vsTarget, vsCode, err)) {
            Log(L"Vertex-Shader: %s", err.c_str());
            return false;
        }
        WriteTextFile(vsCache, vsCode);
    }
    if (FAILED(dev_->CreateVertexShader(vsCode.data(), vsCode.size(), nullptr, &vs_))) return false;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev_->CreateSamplerState(&sd, &sampler_);

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(ShaderConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev_->CreateBuffer(&bd, nullptr, &cbuf_);
    return cbuf_ && sampler_;
}

void Gfx::Shutdown() {
    programs_.clear();
    errorProgram_.reset();
    mfMgr_.Reset();
    convPs_.Reset();
    convCb_.Reset();
    if (ctx_) { ctx_->ClearState(); ctx_->Flush(); }
    cbuf_.Reset();
    sampler_.Reset();
    vs_.Reset();
    factory_.Reset();
    ctx_.Reset();
    dev_.Reset();
    UnloadCompiler();
}

IMFDXGIDeviceManager* Gfx::MfManager() {
    if (!mfStarted_) {
        if (!mf::Load() || FAILED(mf::Startup(MF_VERSION, MFSTARTUP_FULL))) return nullptr;
        mfStarted_ = true;
    }
    if (!mfMgr_ && videoSupport_) {
        UINT token = 0;
        if (SUCCEEDED(mf::CreateDXGIDeviceManager(&token, &mfMgr_))) {
            if (FAILED(mfMgr_->ResetDevice(dev_.Get(), token))) mfMgr_.Reset();
        }
    }
    return mfMgr_.Get();
}

bool Gfx::CreateSwapChain(HWND hwnd, int w, int h, bool legacy, ComPtr<IDXGISwapChain1>& sc,
                          ComPtr<ID3D11RenderTargetView>& rtv) {
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = std::max(1, w);
    d.Height = std::max(1, h);
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_STRETCH;   // Renderauflösung < Bildschirm: DWM skaliert kostenlos hoch
    d.SwapEffect = legacy ? DXGI_SWAP_EFFECT_DISCARD : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (legacy) d.BufferCount = 1;
    HRESULT hr = factory_->CreateSwapChainForHwnd(dev_.Get(), hwnd, &d, nullptr, nullptr, &sc);
    if (FAILED(hr) && !legacy) {
        Log(L"Swapchain (Flip) fehlgeschlagen 0x%08X, versuche klassisch", (unsigned)hr);
        d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        d.BufferCount = 1;
        hr = factory_->CreateSwapChainForHwnd(dev_.Get(), hwnd, &d, nullptr, nullptr, &sc);
    }
    if (FAILED(hr)) {
        Log(L"Swapchain fehlgeschlagen 0x%08X", (unsigned)hr);
        return false;
    }
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    ComPtr<ID3D11Texture2D> bb;
    sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    return SUCCEEDED(dev_->CreateRenderTargetView(bb.Get(), nullptr, &rtv));
}

bool Gfx::ResizeSwapChain(IDXGISwapChain1* sc, int w, int h, ComPtr<ID3D11RenderTargetView>& rtv) {
    rtv.Reset();
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    ctx_->Flush();
    if (FAILED(sc->ResizeBuffers(0, std::max(1, w), std::max(1, h), DXGI_FORMAT_UNKNOWN, 0))) return false;
    ComPtr<ID3D11Texture2D> bb;
    sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    return SUCCEEDED(dev_->CreateRenderTargetView(bb.Get(), nullptr, &rtv));
}

bool Gfx::LoadCompiler() {
    if (d3dCompile_) return true;
    compiler_ = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compiler_) compiler_ = LoadLibraryW(L"d3dcompiler_43.dll");
    if (!compiler_) return false;
    d3dCompile_ = (void*)GetProcAddress(compiler_, "D3DCompile");
    return d3dCompile_ != nullptr;
}

void Gfx::UnloadCompiler() {
    if (compiler_) FreeLibrary(compiler_);
    compiler_ = nullptr;
    d3dCompile_ = nullptr;
}

bool Gfx::CompileHlsl(const std::string& src, const char* entry, const char* target, std::string& bytecode,
                      std::wstring& err) {
    if (!LoadCompiler()) {
        err = L"d3dcompiler_47.dll nicht gefunden";
        return false;
    }
    auto fn = reinterpret_cast<pD3DCompile>(d3dCompile_);
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = fn(src.data(), src.size(), "lumen", nullptr, nullptr, entry, target,
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) {
        std::string e((const char*)errors->GetBufferPointer(), errors->GetBufferSize());
        if (FAILED(hr)) err = Utf8ToWide(e);
        errors->Release();
    }
    if (FAILED(hr) || !code) {
        if (err.empty()) err = L"Shader konnte nicht kompiliert werden";
        if (code) code->Release();
        return false;
    }
    bytecode.assign((const char*)code->GetBufferPointer(), code->GetBufferSize());
    code->Release();
    return true;
}

std::shared_ptr<Program> Gfx::GetProgram(const std::string& user, int fitMode, std::wstring& err) {
    const char* target = fl_ >= D3D_FEATURE_LEVEL_11_0 ? "ps_5_0" : "ps_4_0";
    std::string full = "#define LUMEN_FIT " + std::to_string(fitMode) + "\n" + kPrelude + "#line 1 \"wallpaper.hlsl\"\n" +
                       user + "\n" + kEpilogue;
    uint64_t key = Fnv1a(full + target);
    auto it = programs_.find(key);
    if (it != programs_.end()) {
        if (auto p = it->second.lock()) return p;
    }
    std::string code;
    wchar_t name[40];
    swprintf(name, 40, L"ps_%016llx.cso", (unsigned long long)key);
    std::wstring cachePath = JoinPath(CacheDir(), name);
    bool fromCache = ReadTextFile(cachePath, code) && !code.empty();
    if (!fromCache) {
        if (!CompileHlsl(full, "PSMain", target, code, err)) return nullptr;
        WriteTextFile(cachePath, code);
        UnloadCompiler();   // Compiler-DLL nicht dauerhaft im Speicher halten
    }
    auto p = std::make_shared<Program>();
    if (FAILED(dev_->CreatePixelShader(code.data(), code.size(), nullptr, &p->ps))) {
        DeleteFileW(cachePath.c_str());
        err = L"Pixel-Shader konnte nicht erstellt werden";
        return nullptr;
    }
    auto has = [&](const char* s) { return user.find(s) != std::string::npos; };
    p->usesAudio = has("iAudio") || has("band(") || has("bandf(") || has("iBandsPacked");
    p->usesMouse = has("iMouse") || has("iClick");
    p->usesTime = has("iTime") || has("iHour") || has("iFrame") || p->usesAudio || p->usesMouse;
    programs_[key] = p;
    return p;
}

std::shared_ptr<Program> Gfx::ErrorProgram() {
    if (!errorProgram_) {
        std::wstring e;
        errorProgram_ = GetProgram(kErrorShader, 0, e);
    }
    return errorProgram_;
}

void Gfx::Draw(ID3D11RenderTargetView* rtv, int w, int h, Program* prog, Source* src, const ShaderConstants& c) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(cbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, &c, sizeof(c));
        ctx_->Unmap(cbuf_.Get(), 0);
    }
    D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    ctx_->RSSetViewports(1, &vp);
    if (!prog || !prog->ps) {
        const float clear[4] = {0.02f, 0.02f, 0.03f, 1};
        ctx_->ClearRenderTargetView(rtv, clear);
        return;
    }
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetInputLayout(nullptr);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(prog->ps.Get(), nullptr, 0);
    ID3D11Buffer* cb = cbuf_.Get();
    ctx_->PSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* srv = src ? src->Srv() : nullptr;
    ctx_->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* s = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &s);
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);
}

void Gfx::ConvertNV12(ID3D11ShaderResourceView* y, ID3D11ShaderResourceView* uv, ID3D11RenderTargetView* out,
                      UINT w, UINT h, float uvScaleX, float uvScaleY, bool bt709) {
    if (!convPs_) {
        const char* target = fl_ >= D3D_FEATURE_LEVEL_11_0 ? "ps_5_0" : "ps_4_0";
        std::string code;
        std::wstring err;
        std::wstring cache = JoinPath(CacheDir(), L"nv12_" + std::to_wstring(Fnv1a(std::string(kNv12Shader) + target)) + L".cso");
        if (!ReadTextFile(cache, code) || code.empty()) {
            if (!CompileHlsl(kNv12Shader, "PSConv", target, code, err)) {
                Log(L"NV12-Shader: %s", err.c_str());
                return;
            }
            WriteTextFile(cache, code);
            UnloadCompiler();
        }
        dev_->CreatePixelShader(code.data(), code.size(), nullptr, &convPs_);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 32;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev_->CreateBuffer(&bd, nullptr, &convCb_);
        if (!convPs_ || !convCb_) return;
    }
    float cb[8] = {1.0f / w, 1.0f / h, uvScaleX, uvScaleY, bt709 ? 1.0f : 0.0f, 0, 0, 0};
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(convCb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, cb, sizeof(cb));
        ctx_->Unmap(convCb_.Get(), 0);
    }
    D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
    ctx_->OMSetRenderTargets(1, &out, nullptr);
    ctx_->RSSetViewports(1, &vp);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetInputLayout(nullptr);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(convPs_.Get(), nullptr, 0);
    ID3D11Buffer* b = convCb_.Get();
    ctx_->PSSetConstantBuffers(0, 1, &b);
    ID3D11ShaderResourceView* srvs[2] = {y, uv};
    ctx_->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* sm = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &sm);
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 2, none);
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
}

void Gfx::Trim() {
    for (auto it = programs_.begin(); it != programs_.end();) {
        if (it->second.expired()) it = programs_.erase(it);
        else ++it;
    }
    ComPtr<IDXGIDevice3> d3;
    if (ctx_) ctx_->ClearState();
    if (SUCCEEDED(dev_.As(&d3))) d3->Trim();   // gibt Treiber-Zwischenspeicher frei
}

// =====================================================================================
//  Bild (WIC)
// =====================================================================================
class ImageSource : public Source {
public:
    ComPtr<ID3D11ShaderResourceView> srv;
    int w = 0, h = 0;
    ID3D11ShaderResourceView* Srv() override { return srv.Get(); }
    int Width() const override { return w; }
    int Height() const override { return h; }
};

std::shared_ptr<Source> LoadImageSource(Gfx& gfx, const std::wstring& path, std::wstring& err) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        err = L"WIC nicht verfügbar";
        return nullptr;
    }
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
        err = L"Bild kann nicht geöffnet werden: " + FileNameOf(path);
        return nullptr;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) { err = L"Bild ist leer"; return nullptr; }
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    ComPtr<IWICBitmapSource> srcBmp = frame;
    const UINT maxDim = 8192;
    if (w > maxDim || h > maxDim) {
        double s = std::min((double)maxDim / w, (double)maxDim / h);
        UINT nw = std::max(1u, (UINT)(w * s)), nh = std::max(1u, (UINT)(h * s));
        ComPtr<IWICBitmapScaler> sc;
        wic->CreateBitmapScaler(&sc);
        sc->Initialize(srcBmp.Get(), nw, nh, WICBitmapInterpolationModeFant);
        srcBmp = sc;
        w = nw;
        h = nh;
    }
    ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(srcBmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom))) {
        err = L"Bildformat wird nicht unterstützt";
        return nullptr;
    }
    std::vector<uint8_t> px((size_t)w * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) { err = L"Bild unlesbar"; return nullptr; }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{px.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(gfx.Dev()->CreateTexture2D(&td, &init, &tex))) { err = L"Bild zu groß für die Grafikkarte"; return nullptr; }
    auto img = std::make_shared<ImageSource>();
    gfx.Dev()->CreateShaderResourceView(tex.Get(), nullptr, &img->srv);
    img->w = (int)w;
    img->h = (int)h;
    return img;
}

// =====================================================================================
//  Video (Media Foundation Source Reader, Hardware-Dekodierung, ohne Ton)
// =====================================================================================
class VideoSource : public Source {
public:
    Gfx* gfx = nullptr;
    ComPtr<IMFSourceReader> reader;
    // Ergebnis, das Shader sehen (BGRA/BGRX)
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;   // nur im NV12-Weg
    DXGI_FORMAT texFormat = DXGI_FORMAT_UNKNOWN;
    // NV12-Weg: Ebenen
    bool nv12 = false;
    ComPtr<ID3D11Texture2D> nvTex, yTex, uvTex;
    ComPtr<ID3D11ShaderResourceView> ySrv, uvSrv;
    UINT nvW = 0, nvH = 0;

    UINT frameW = 0, frameH = 0;
    LONG defaultStride = 0;
    double videoTime = 0, nextTs = 0, lastNow = -1, speed = 1.0;
    bool failed = false, haveFrame = false;

    ID3D11ShaderResourceView* Srv() override { return srv.Get(); }
    int Width() const override { return (int)frameW; }
    int Height() const override { return (int)frameH; }
    bool Animated() const override { return !failed; }
    void SetSpeed(double s) override { speed = std::clamp(s, 0.1, 4.0); }

    bool TrySubtype(const GUID& sub) {
        ComPtr<IMFMediaType> mt;
        mf::CreateMediaType(&mt);
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        mt->SetGUID(MF_MT_SUBTYPE, sub);
        return SUCCEEDED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt.Get()));
    }

    bool Open(const std::wstring& path, std::wstring& err) {
        IMFDXGIDeviceManager* mgr = gfx->MfManager();
        if (!mf::Load()) {
            err = L"Videos brauchen Media Foundation (bei Windows N: Media Feature Pack installieren)";
            return false;
        }
        for (int attempt = 0; attempt < 2; attempt++) {
            // Versuch 1: Decoder wandelt selbst in RGB. Versuch 2: NV12, Lumen wandelt per Shader.
            ComPtr<IMFAttributes> attr;
            mf::CreateAttributes(&attr, 4);
            if (mgr) attr->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, mgr);
            attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            if (attempt == 0) attr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
            reader.Reset();
            HRESULT hr = mf::CreateSourceReaderFromURL(path.c_str(), attr.Get(), &reader);
            if (FAILED(hr)) {
                err = L"Video kann nicht geöffnet werden: " + FileNameOf(path);
                Log(L"Video: MFCreateSourceReaderFromURL 0x%08X", (unsigned)hr);
                return false;
            }
            reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);   // Ton wird gar nicht dekodiert
            reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
            if (attempt == 0 && (TrySubtype(MFVideoFormat_RGB32) || TrySubtype(MFVideoFormat_ARGB32))) {
                nv12 = false;
                break;
            }
            if (attempt == 1 && TrySubtype(MFVideoFormat_NV12)) {
                nv12 = true;
                Log(L"Video: nutze NV12 mit eigener Farbwandlung");
                break;
            }
            if (attempt == 1) {
                err = L"Videoformat wird nicht unterstützt (Codec fehlt?): " + FileNameOf(path);
                return false;
            }
        }
        ReadFrameSize();
        ReadOne();   // erstes Bild sofort
        return !failed;
    }

    void ReadFrameSize() {
        ComPtr<IMFMediaType> cur;
        if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) {
            MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &frameW, &frameH);
            UINT32 stride = 0;
            if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) defaultStride = (LONG)stride;
        }
    }

    void EnsureTexture(DXGI_FORMAT fmt, UINT w, UINT h, bool renderTarget) {
        if (tex && fmt == texFormat) {
            D3D11_TEXTURE2D_DESC d;
            tex->GetDesc(&d);
            if (d.Width == w && d.Height == h) return;
        }
        tex.Reset();
        srv.Reset();
        rtv.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (renderTarget ? D3D11_BIND_RENDER_TARGET : 0);
        if (SUCCEEDED(gfx->Dev()->CreateTexture2D(&td, nullptr, &tex))) {
            gfx->Dev()->CreateShaderResourceView(tex.Get(), nullptr, &srv);
            if (renderTarget) gfx->Dev()->CreateRenderTargetView(tex.Get(), nullptr, &rtv);
        }
        texFormat = fmt;
    }

    static ComPtr<ID3D11Texture2D> MakeTex(ID3D11Device* dev, DXGI_FORMAT f, UINT w, UINT h, ComPtr<ID3D11ShaderResourceView>& srvOut) {
        ComPtr<ID3D11Texture2D> t;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = std::max(1u, w);
        td.Height = std::max(1u, h);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = f;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &t))) dev->CreateShaderResourceView(t.Get(), nullptr, &srvOut);
        return t;
    }

    // NV12 aus der Grafikkarte: Textur kopieren, Ebenen als R8 / R8G8 lesen
    bool EnsureNv12Gpu(UINT w, UINT h) {
        if (nvTex && nvW == w && nvH == h) return true;
        nvTex.Reset(); ySrv.Reset(); uvSrv.Reset(); yTex.Reset(); uvTex.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_NV12;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(gfx->Dev()->CreateTexture2D(&td, nullptr, &nvTex))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        sd.Format = DXGI_FORMAT_R8_UNORM;
        gfx->Dev()->CreateShaderResourceView(nvTex.Get(), &sd, &ySrv);
        sd.Format = DXGI_FORMAT_R8G8_UNORM;
        gfx->Dev()->CreateShaderResourceView(nvTex.Get(), &sd, &uvSrv);
        nvW = w;
        nvH = h;
        return ySrv && uvSrv;
    }

    // NV12 aus dem Arbeitsspeicher: zwei getrennte Texturen
    bool EnsureNv12Cpu(UINT w, UINT h) {
        if (yTex && !nvTex && nvW == w && nvH == h) return true;
        nvTex.Reset(); ySrv.Reset(); uvSrv.Reset();
        yTex = MakeTex(gfx->Dev(), DXGI_FORMAT_R8_UNORM, w, h, ySrv);
        uvTex = MakeTex(gfx->Dev(), DXGI_FORMAT_R8G8_UNORM, (w + 1) / 2, (h + 1) / 2, uvSrv);
        nvW = w;
        nvH = h;
        return ySrv && uvSrv;
    }

    // Notweg: Decoder-Textur -> Staging -> zwei Ebenen-Texturen
    bool gpuNvFailed = false;
    ComPtr<ID3D11Texture2D> staging;
    void CopyNv12ViaStaging(ID3D11Texture2D* src, UINT sub, const D3D11_TEXTURE2D_DESC& d) {
        if (staging) {
            D3D11_TEXTURE2D_DESC sd;
            staging->GetDesc(&sd);
            if (sd.Width != d.Width || sd.Height != d.Height) staging.Reset();
        }
        if (!staging) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = d.Width;
            td.Height = d.Height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_NV12;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_STAGING;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(gfx->Dev()->CreateTexture2D(&td, nullptr, &staging))) {
                Log(L"Video: NV12-Staging nicht möglich");
                failed = true;
                return;
            }
        }
        gfx->Ctx()->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, src, sub, nullptr);
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(gfx->Ctx()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
        if (EnsureNv12Cpu(frameW, frameH)) {
            const BYTE* base = (const BYTE*)m.pData;
            gfx->Ctx()->UpdateSubresource(yTex.Get(), 0, nullptr, base, m.RowPitch, 0);
            gfx->Ctx()->UpdateSubresource(uvTex.Get(), 0, nullptr, base + (size_t)m.RowPitch * d.Height, m.RowPitch, 0);
        }
        gfx->Ctx()->Unmap(staging.Get(), 0);
        ConvertNv12(frameW, frameH);
    }

    void ConvertNv12(UINT surfW, UINT surfH) {
        EnsureTexture(DXGI_FORMAT_B8G8R8A8_UNORM, frameW, frameH, true);
        if (!rtv) return;
        gfx->ConvertNV12(ySrv.Get(), uvSrv.Get(), rtv.Get(), frameW, frameH, float(frameW) / surfW, float(frameH) / surfH,
                         frameH >= 720);
        haveFrame = true;
    }

    void Rewind() {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I8;
        pv.hVal.QuadPart = 0;
        reader->SetCurrentPosition(GUID_NULL, pv);
        videoTime = 0;
        nextTs = 0;
    }

    void ReadOne() {
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            Log(L"Video: ReadSample 0x%08X", (unsigned)hr);
            failed = true;
            return;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) ReadFrameSize();
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Rewind();   // Endlosschleife
            return;
        }
        if (!sample || !frameW || !frameH) return;
        LONGLONG dur = 0;
        if (FAILED(sample->GetSampleDuration(&dur)) || dur <= 0) dur = 333333;
        nextTs = double(ts + dur) / 1e7;

        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->GetBufferByIndex(0, &buf))) return;

        // --- Frame liegt auf der Grafikkarte ---
        ComPtr<IMFDXGIBuffer> dx;
        if (SUCCEEDED(buf.As(&dx))) {
            ComPtr<ID3D11Texture2D> t;
            UINT sub = 0;
            if (FAILED(dx->GetResource(IID_PPV_ARGS(&t))) || FAILED(dx->GetSubresourceIndex(&sub))) return;
            D3D11_TEXTURE2D_DESC d;
            t->GetDesc(&d);
            if (d.Format == DXGI_FORMAT_NV12) {
                if (!gpuNvFailed && EnsureNv12Gpu(d.Width, d.Height)) {
                    gfx->Ctx()->CopySubresourceRegion(nvTex.Get(), 0, 0, 0, 0, t.Get(), sub, nullptr);
                    ConvertNv12(d.Width, d.Height);
                    return;
                }
                if (!gpuNvFailed) Log(L"Video: NV12 nicht direkt lesbar, nutze Umweg über den Arbeitsspeicher");
                gpuNvFailed = true;
                CopyNv12ViaStaging(t.Get(), sub, d);
                return;
            }
            UINT w = std::min(frameW, d.Width), h = std::min(frameH, d.Height);
            EnsureTexture(d.Format, w, h, false);
            if (!tex) return;
            D3D11_BOX box{0, 0, 0, w, h, 1};
            gfx->Ctx()->CopySubresourceRegion(tex.Get(), 0, 0, 0, 0, t.Get(), sub, &box);
            haveFrame = true;
            return;
        }

        // --- Software-Dekodierung: Pixel hochladen ---
        BYTE* scan = nullptr;
        LONG pitch = 0;
        DWORD bufLen = 0;
        bool locked2d = false;
        ComPtr<IMF2DBuffer2> b2;
        ComPtr<IMF2DBuffer> b1;
        BYTE* bufStart = nullptr;
        if (SUCCEEDED(buf.As(&b2)) && SUCCEEDED(b2->Lock2DSize(MF2DBuffer_LockFlags_Read, &scan, &pitch, &bufStart, &bufLen))) {
            locked2d = true;
        } else if (SUCCEEDED(buf.As(&b1)) && SUCCEEDED(b1->Lock2D(&scan, &pitch))) {
            locked2d = true;
            b1->GetContiguousLength(&bufLen);
        } else {
            DWORD maxLen = 0;
            if (FAILED(buf->Lock(&scan, &maxLen, &bufLen))) return;
            pitch = defaultStride ? defaultStride : (LONG)(nv12 ? frameW : frameW * 4);
        }

        if (nv12) {
            LONG ap = std::abs(pitch);
            UINT rows = ap ? (UINT)(bufLen / ap) : 0;
            UINT alignedH = rows * 2 / 3;
            if (alignedH < frameH) alignedH = frameH;
            if (EnsureNv12Cpu(frameW, frameH) && pitch > 0) {
                gfx->Ctx()->UpdateSubresource(yTex.Get(), 0, nullptr, scan, (UINT)pitch, 0);
                gfx->Ctx()->UpdateSubresource(uvTex.Get(), 0, nullptr, scan + (size_t)pitch * alignedH, (UINT)pitch, 0);
                ConvertNv12(frameW, frameH);
            }
        } else {
            EnsureTexture(DXGI_FORMAT_B8G8R8X8_UNORM, frameW, frameH, false);
            if (tex) {
                if (pitch > 0) {
                    gfx->Ctx()->UpdateSubresource(tex.Get(), 0, nullptr, scan, (UINT)pitch, 0);
                } else {
                    std::vector<uint8_t> tmp((size_t)frameW * 4 * frameH);
                    for (UINT y = 0; y < frameH; y++) memcpy(&tmp[(size_t)y * frameW * 4], scan + (LONG)y * pitch, frameW * 4);
                    gfx->Ctx()->UpdateSubresource(tex.Get(), 0, nullptr, tmp.data(), frameW * 4, 0);
                }
                haveFrame = true;
            }
        }
        if (b2 && locked2d) b2->Unlock2D();
        else if (b1 && locked2d) b1->Unlock2D();
        else buf->Unlock();
    }

    void Update(double now, bool active) override {
        if (failed) return;
        if (lastNow < 0) lastNow = now;
        double dt = std::min(now - lastNow, 0.25);
        lastNow = now;
        if (!active) return;
        videoTime += dt * speed;
        int reads = 0;
        while (videoTime >= nextTs && reads < 4 && !failed) {
            ReadOne();
            reads++;
        }
        if (videoTime - nextTs > 0.5) videoTime = nextTs;   // nicht hinterherhetzen
    }
};

std::shared_ptr<Source> OpenVideoSource(Gfx& gfx, const std::wstring& path, std::wstring& err) {
    auto v = std::make_shared<VideoSource>();
    v->gfx = &gfx;
    if (!v->Open(path, err)) return nullptr;
    return v;
}
