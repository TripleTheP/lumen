#include "audio.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cmath>
#include <complex>
#include <algorithm>

static const GUID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID kIID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID kIID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID kIID_IAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};

void AudioAnalyzer::Start() {
    if (running_) return;
    running_ = true;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { ThreadMain(); });
    Log(L"Audio: gestartet");
}

void AudioAnalyzer::Stop() {
    if (!running_) return;
    running_ = false;
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
    std::lock_guard<std::mutex> lk(mtx_);
    frame_ = AudioFrame{};
    Log(L"Audio: gestoppt");
}

AudioFrame AudioAnalyzer::Get() {
    std::lock_guard<std::mutex> lk(mtx_);
    return frame_;
}

void AudioAnalyzer::ThreadMain() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (running_) {
        if (!CaptureSession()) {
            // Kein Gerät oder Fehler: kurz warten und neu versuchen
            if (WaitForSingleObject(stopEvent_, 1500) == WAIT_OBJECT_0) break;
        }
    }
    CoUninitialize();
}

static std::wstring DefaultDeviceId(IMMDeviceEnumerator* en) {
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return L"";
    LPWSTR id = nullptr;
    std::wstring r;
    if (SUCCEEDED(dev->GetId(&id)) && id) r = id;
    if (id) CoTaskMemFree(id);
    return r;
}

bool AudioAnalyzer::CaptureSession() {
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en)))
        return false;
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return false;
    std::wstring devId = DefaultDeviceId(en.Get());

    ComPtr<IAudioClient> client;
    if (FAILED(dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&client))) return false;
    WAVEFORMATEX* wfx = nullptr;
    if (FAILED(client->GetMixFormat(&wfx)) || !wfx) return false;

    bool isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx);
        isFloat = ext->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
    }
    const int channels = wfx->nChannels;
    const int bits = wfx->wBitsPerSample;
    sampleRate_ = (int)wfx->nSamplesPerSec;

    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, wfx, nullptr);
    CoTaskMemFree(wfx);
    if (FAILED(hr)) {
        Log(L"Audio: Initialize fehlgeschlagen 0x%08X", (unsigned)hr);
        return false;
    }
    ComPtr<IAudioCaptureClient> cap;
    if (FAILED(client->GetService(kIID_IAudioCaptureClient, (void**)&cap))) return false;
    if (FAILED(client->Start())) return false;

    double lastDevCheck = NowSeconds();
    bool ok = true;
    while (running_) {
        if (WaitForSingleObject(stopEvent_, 15) == WAIT_OBJECT_0) break;

        UINT32 packet = 0;
        hr = cap->GetNextPacketSize(&packet);
        if (FAILED(hr)) { ok = false; break; }
        bool gotData = false;
        while (packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) { ok = false; break; }
            for (UINT32 i = 0; i < frames; i++) {
                float s = 0;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    for (int c = 0; c < channels && c < 2; c++) {
                        size_t idx = (size_t)i * channels + c;
                        if (isFloat && bits == 32) s += reinterpret_cast<float*>(data)[idx];
                        else if (bits == 16) s += reinterpret_cast<int16_t*>(data)[idx] / 32768.0f;
                        else if (bits == 32) s += reinterpret_cast<int32_t*>(data)[idx] / 2147483648.0f;
                        else if (bits == 24) {
                            const uint8_t* p = data + idx * 3;
                            int32_t v = (p[0] << 8 | p[1] << 16 | p[2] << 24) >> 8;
                            s += v / 8388608.0f;
                        }
                    }
                    s /= (float)std::min(channels, 2);
                }
                ring_[ringPos_] = s;
                ringPos_ = (ringPos_ + 1) % kFft;
            }
            cap->ReleaseBuffer(frames);
            gotData = true;
            if (FAILED(cap->GetNextPacketSize(&packet))) { ok = false; break; }
        }
        if (!ok) break;
        if (!gotData) {
            // Stille: Puffer langsam leeren, damit die Bänder abklingen
            for (int i = 0; i < kFft / 16; i++) { ring_[ringPos_] = 0; ringPos_ = (ringPos_ + 1) % kFft; }
        }
        Analyze();

        if (NowSeconds() - lastDevCheck > 2.0) {
            lastDevCheck = NowSeconds();
            if (DefaultDeviceId(en.Get()) != devId) break;   // Ausgabegerät gewechselt → neu starten
        }
    }
    client->Stop();
    return ok;
}

static void Fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * 3.14159265358979f / (float)len;
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1, 0);
            for (size_t k = 0; k < len / 2; k++) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

void AudioAnalyzer::Analyze() {
    static std::vector<std::complex<float>> buf(kFft);
    double rms = 0;
    for (int i = 0; i < kFft; i++) {
        float s = ring_[(ringPos_ + i) % kFft];
        rms += s * s;
        float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (kFft - 1));
        buf[i] = std::complex<float>(s * w, 0);
    }
    rms = std::sqrt(rms / kFft);
    Fft(buf);

    // 16 logarithmisch verteilte Bänder von 30 Hz bis 16 kHz
    float bands[16];
    float maxBand = 0;
    const float fMin = 30.f, fMax = 16000.f;
    const float binHz = (float)sampleRate_ / kFft;
    for (int b = 0; b < 16; b++) {
        float f0 = fMin * std::pow(fMax / fMin, b / 16.f);
        float f1 = fMin * std::pow(fMax / fMin, (b + 1) / 16.f);
        int i0 = std::max(1, (int)(f0 / binHz));
        int i1 = std::max(i0 + 1, (int)(f1 / binHz));
        i1 = std::min(i1, kFft / 2);
        float sum = 0;
        for (int i = i0; i < i1; i++) sum += std::norm(buf[i]);
        float amp = std::sqrt(sum / std::max(1, i1 - i0)) / (kFft / 4.0f);
        amp *= 1.0f + b * 0.18f;   // Höhen sind von Natur aus leiser
        bands[b] = amp;
        maxBand = std::max(maxBand, amp);
    }
    // Automatische Lautstärke-Anpassung, aber Stille nicht hochverstärken
    peak_ = std::max({peak_ * 0.997f, maxBand, 0.01f});

    AudioFrame f;
    for (int b = 0; b < 16; b++) {
        float v = std::clamp(bands[b] / peak_, 0.0f, 1.0f);
        float k = v > smooth_[b] ? 0.6f : 0.15f;   // schnell hoch, langsam runter
        smooth_[b] += (v - smooth_[b]) * k;
        f.bands[b] = smooth_[b];
    }
    auto avg = [&](int a, int z) { float s = 0; for (int i = a; i <= z; i++) s += f.bands[i]; return s / (z - a + 1); };
    f.bass = avg(0, 3);
    f.mid = avg(4, 9);
    f.treble = avg(10, 15);
    f.level = std::clamp((float)rms * 4.0f, 0.0f, 1.0f);

    std::lock_guard<std::mutex> lk(mtx_);
    frame_ = f;
}
