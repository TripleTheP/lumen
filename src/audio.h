#pragma once
#include "common.h"
#include <atomic>
#include <thread>
#include <mutex>

struct AudioFrame {
    float bass = 0, mid = 0, treble = 0, level = 0;
    float bands[16] = {};
};

// Fängt per WASAPI-Loopback ab, was gerade abgespielt wird, und liefert 16 Frequenzbänder.
// Läuft nur, solange ein Hintergrund Audio braucht.
class AudioAnalyzer {
public:
    ~AudioAnalyzer() { Stop(); }
    void Start();
    void Stop();
    bool Running() const { return running_; }
    AudioFrame Get();

private:
    void ThreadMain();
    bool CaptureSession();   // läuft bis Fehler oder Stopp
    void Analyze();

    std::thread thread_;
    std::atomic<bool> running_{false};
    HANDLE stopEvent_ = nullptr;

    std::mutex mtx_;
    AudioFrame frame_;

    // Ringpuffer (Mono)
    static const int kFft = 1024;
    float ring_[kFft * 2] = {};
    int ringPos_ = 0;
    int sampleRate_ = 48000;
    float smooth_[16] = {};
    float peak_ = 0.05f;
};
