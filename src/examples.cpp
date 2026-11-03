#include "examples.h"

// ---------------------------------------------------------------------------------
// Aurora – Nordlichter, reagieren auf Bass und Maus
// ---------------------------------------------------------------------------------
static const char* kAuroraIni = R"(name=Aurora
shader=shader.hlsl
)";

static const char* kAurora = R"HLSL(// Aurora – Nordlichter über Bergen.
// Reagiert auf Musik (Bass macht die Lichter heller) und auf die Maus (sanfter Schein).

float4 mainImage(float2 fragCoord) {
    float2 uv = fragCoord / iResolution.y;          // Seitenverhältnis bleibt erhalten
    float  w  = iResolution.x / iResolution.y;
    float  t  = iTime * 0.04;
    float  yN = fragCoord.y / iResolution.y;        // 0 oben, 1 unten

    float3 col = lerp(float3(0.005, 0.01, 0.04), float3(0.01, 0.05, 0.10), yN);

    // Sterne
    float s = L_hash(floor(fragCoord / 2.0));
    col += step(0.9982, s) * (0.5 + 0.5 * sin(iTime * 2.0 + s * 90.0)) * (1.0 - yN);

    float energy = 0.55 + iAudio.x * 1.3 + iAudio.w * 0.4;

    [unroll] for (int k = 0; k < 3; k++) {
        float fk = k;
        float ln = 0.30 + 0.09 * fk + 0.14 * L_fbm(float2(uv.x * 1.3 + t * (1.0 + fk * 0.4), fk * 3.7 + t));
        float d = ln - uv.y;                         // > 0 oberhalb der Linie
        float curtain = smoothstep(-0.015, 0.0, d) * exp(-max(d, 0.0) * (4.5 - band(k * 2) * 2.0));
        float rays = 0.55 + 0.45 * L_fbm(float2(uv.x * 14.0 + t * 6.0 + fk * 9.0, uv.y * 1.5));
        float3 c = lerp(float3(0.1, 1.0, 0.55), float3(0.55, 0.3, 1.0), saturate(fk * 0.4 + 0.3 * sin(t * 4.0 + uv.x * 1.5)));
        col += c * curtain * rays * 0.33 * energy;
    }

    // Schein unter der Maus
    if (iMouse.w > 0.5) {
        float md = length(uv - iMouse.xy / iResolution.y);
        col += float3(0.3, 0.8, 1.0) * 0.06 / (md * md * 60.0 + 1.0);
    }

    // Berge
    float hill = 0.80 + 0.07 * (L_fbm(float2(uv.x * 2.2, 3.0)) - 0.5) + 0.03 * sin(uv.x * 1.7);
    col = lerp(col, float3(0.004, 0.008, 0.016), smoothstep(hill - 0.002, hill + 0.002, uv.y));
    float hill2 = 0.88 + 0.05 * (L_fbm(float2(uv.x * 3.1 + 5.0, 9.0)) - 0.5);
    col = lerp(col, float3(0.0, 0.002, 0.006), smoothstep(hill2 - 0.002, hill2 + 0.002, uv.y));
    return float4(col, 1.0);
}
)HLSL";

// ---------------------------------------------------------------------------------
// Punktfeld – interaktiv: Maus schiebt Punkte weg, Klick erzeugt Welle
// ---------------------------------------------------------------------------------
static const char* kDotsIni = R"(name=Punktfeld (interaktiv)
shader=shader.hlsl
)";

static const char* kDots = R"HLSL(// Punktfeld – die Maus schiebt Punkte beiseite, ein Klick auf den Desktop löst eine Welle aus.
// Leichter Puls im Takt des Basses.

float4 mainImage(float2 fragCoord) {
    float s = iResolution.y / 36.0;                  // Abstand der Punkte in Pixeln
    float2 base = floor(fragCoord / s);
    float3 col = lerp(float3(0.02, 0.02, 0.05), float3(0.05, 0.02, 0.08), fragCoord.y / iResolution.y);

    float rt    = iClick.z;
    float waveR = rt * 900.0;
    float fade  = iClick.w > 0.0 ? exp(-rt * 1.2) : 0.0;

    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        float2 c  = (base + float2(x, y) + 0.5) * s;
        float2 dm = c - iMouse.xy;
        float  lm = length(dm) + 1e-3;
        float  push = iMouse.w * s * 0.85 * exp(-lm / (s * 6.0));
        float2 dc = c - iClick.xy;
        float  lc = length(dc) + 1e-3;
        float  wave = fade * s * 0.6 * sin((lc - waveR) / (s * 1.2)) * exp(-abs(lc - waveR) / (s * 4.0));
        float2 p = c + dm / lm * push + dc / lc * wave;
        float  energy = push / (s * 0.85) + abs(wave) / (s * 0.6);
        float  r = s * (0.08 + 0.12 * energy + 0.05 * iAudio.x);
        float  d = length(fragCoord - p);
        float3 dotc = lerp(float3(0.30, 0.34, 0.55), L_hue(frac(iTime * 0.03 + lc * 0.0005 + lm * 0.0008)), saturate(energy * 1.5 + 0.15));
        col += dotc * (smoothstep(r, r - 1.5, d) + 0.25 * exp(-d / (r * 2.5)) * energy);
    }
    return float4(col, 1.0);
}
)HLSL";

// ---------------------------------------------------------------------------------
// Spektrum – Equalizer aus 16 Bändern
// ---------------------------------------------------------------------------------
static const char* kSpectrumIni = R"(name=Spektrum (Musik)
shader=shader.hlsl
)";

static const char* kSpectrum = R"HLSL(// Spektrum – 16 Frequenzbänder als leuchtende Säulen mit Spiegelung.
// Spielt keine Musik, ruhen die Säulen. Die Säule unter der Maus leuchtet heller.

float4 mainImage(float2 fragCoord) {
    float2 uv  = fragCoord / iResolution;
    float  asp = iResolution.x / iResolution.y;
    float3 col = lerp(float3(0.04, 0.02, 0.09), float3(0.01, 0.01, 0.02), uv.y);

    float2 c = (uv - float2(0.5, 0.45)) * float2(asp, 1.0);
    col += float3(0.35, 0.10, 0.55) * iAudio.x * 0.5 * exp(-length(c) * 2.5);

    const float x0 = 0.12, x1 = 0.88, floorY = 0.66;
    float w  = (x1 - x0) / 16.0;
    float fx = (uv.x - x0) / w;
    int   i  = (int)floor(fx);
    if (i >= 0 && i < 16) {
        float lx  = frac(fx);
        float hgt = 0.015 + band(i) * 0.45;
        float3 bc = L_hue(0.75 - i / 16.0 * 0.55 + iTime * 0.01);
        float mouseHere = (iMouse.w > 0.5 && abs(iMouse.x / iResolution.x - (x0 + (i + 0.5) * w)) < w * 0.5) ? 1.0 : 0.0;
        bc *= 1.0 + mouseHere * 0.6;
        float inX  = smoothstep(0.12, 0.16, lx) * smoothstep(0.88, 0.84, lx);
        float up   = floorY - uv.y;
        float bar  = inX * step(0.0, up) * smoothstep(hgt, hgt - 0.004, up);
        float grad = 0.55 + 0.45 * saturate(up / max(hgt, 1e-3));
        col += bc * bar * grad;
        col += bc * inX * 0.35 * exp(-abs(up - hgt) * 60.0) * step(0.0, up);
        float down = -up;
        col += bc * inX * step(0.0, down) * smoothstep(hgt * 0.5, 0.0, down) * 0.18;
    }
    col += float3(0.3, 0.25, 0.5) * 0.25 * exp(-abs(uv.y - floorY) * 400.0) * step(x0, uv.x) * step(uv.x, x1);
    return float4(col, 1.0);
}
)HLSL";

// ---------------------------------------------------------------------------------
// Tag und Nacht – folgt der Uhrzeit
// ---------------------------------------------------------------------------------
static const char* kDayNightIni = R"(name=Tag und Nacht
shader=shader.hlsl
fps=20
)";

static const char* kDayNight = R"HLSL(// Tag und Nacht – Himmel, Sonne und Mond folgen deiner Uhr.
// Nachts folgt ein kleines Glühwürmchen-Licht der Maus.

float4 mainImage(float2 fragCoord) {
    float2 uv  = fragCoord / iResolution;
    float  asp = iResolution.x / iResolution.y;
    float  h   = iHour;

    float dayT  = saturate((h - 6.0) / 12.0);
    float isDay = step(6.0, h) * step(h, 18.0);
    float sunH  = sin(dayT * 3.14159) * isDay;
    float light = saturate(sunH * 2.2 + 0.05);
    float dusk  = exp(-pow((h - 18.3) / 1.0, 2.0)) + exp(-pow((h - 5.8) / 1.0, 2.0));

    float3 top = lerp(float3(0.01, 0.015, 0.05), float3(0.18, 0.42, 0.85), light);
    float3 bot = lerp(float3(0.03, 0.05, 0.12), float3(0.65, 0.82, 0.98), light);
    bot = lerp(bot, float3(1.0, 0.45, 0.2), saturate(dusk) * 0.85);
    top = lerp(top, float3(0.25, 0.15, 0.35), saturate(dusk) * 0.5);
    float3 col = lerp(top, bot, pow(uv.y, 1.3));

    // Sterne
    float night = 1.0 - saturate(light * 3.0);
    float s  = L_hash(floor(fragCoord / 2.5));
    float tw = 0.6 + 0.4 * sin(iTime * (1.0 + s * 3.0) + s * 50.0);
    col += step(0.9975, s) * tw * night * (1.0 - uv.y);

    // Sonne
    float2 sunPos = float2(lerp(0.12, 0.88, dayT), 0.78 - sunH * 0.6);
    float  sd = length((uv - sunPos) * float2(asp, 1.0));
    float3 sunCol = lerp(float3(1.0, 0.55, 0.25), float3(1.0, 0.95, 0.8), saturate(sunH * 2.0));
    col += isDay * (sunCol * smoothstep(0.045, 0.04, sd) + sunCol * 0.25 * exp(-sd * 7.0));

    // Mond (Sichel)
    float nightT = h >= 18.0 ? (h - 18.0) / 12.0 : (h + 6.0) / 12.0;
    float moonH  = sin(saturate(nightT) * 3.14159) * (1.0 - isDay);
    float2 moonPos = float2(lerp(0.15, 0.85, saturate(nightT)), 0.78 - moonH * 0.55);
    float2 md = (uv - moonPos) * float2(asp, 1.0);
    float moon = smoothstep(0.032, 0.028, length(md)) * (1.0 - smoothstep(0.029, 0.025, length(md - float2(0.012, -0.006))));
    col += (1.0 - isDay) * (float3(0.9, 0.92, 1.0) * moon + float3(0.3, 0.35, 0.5) * 0.12 * exp(-length(md) * 9.0));

    // Hügel in drei Ebenen
    float x = uv.x * asp;
    [unroll] for (int i = 0; i < 3; i++) {
        float fi = i;
        float hy = 0.62 + fi * 0.1 + 0.12 * (L_fbm(float2(x * (1.2 + fi * 0.6) + fi * 13.0, fi * 7.0)) - 0.5);
        float3 hc = lerp(float3(0.02, 0.03, 0.06), float3(0.25, 0.45, 0.35), light) * (1.0 - fi * 0.28);
        hc = lerp(hc, col, 0.35 - fi * 0.12);
        col = lerp(col, hc, smoothstep(hy - 0.002, hy + 0.002, uv.y));
    }

    // Glühwürmchen an der Maus (nur nachts)
    if (iMouse.w > 0.5) {
        float2 dm = (fragCoord - iMouse.xy) / iResolution.y;
        col += night * float3(1.0, 0.8, 0.35) * 0.05 / (1.0 + dot(dm, dm) * 4000.0);
    }
    return float4(col, 1.0);
}
)HLSL";

static const char* kTemplate = R"HLSL(// Mein Shader – Vorlage
// Speichern genügt: Lumen lädt den Shader automatisch neu.
//
// Verfügbare Werte (Pixel-Koordinaten, oben links = 0,0):
//   iResolution  Größe in Pixeln            iTime       Sekunden seit Start
//   iMouse       xy Maus, z Taste, w Maus über dem Desktop (1/0)
//   iClick       xy letzter Klick, z Sekunden seit Klick, w Anzahl Klicks
//   iAudio       x Bass, y Mitten, z Höhen, w Pegel (0..1)
//   band(i)      Frequenzband 0..15         bandf(x)    Band interpoliert, x = 0..1
//   iHour        Uhrzeit 0..24              iChannel0   Bild/Video aus "source=" in wallpaper.ini
//   texFit(fragCoord)  Bild/Video passend eingepasst
// Helfer: L_hash, L_noise, L_fbm, L_hue
//
// Tipp: Code von shadertoy.com lässt sich meist übertragen: vec2 -> float2, mix -> lerp,
// fract -> frac, mod -> fmod. Achtung: dort ist y = 0 unten, hier oben.

float4 mainImage(float2 fragCoord) {
    float2 uv = fragCoord / iResolution;
    float3 col = 0.5 + 0.5 * cos(iTime * 0.5 + uv.xyx * 3.0 + float3(0, 2, 4));
    col *= 0.6 + 0.6 * iAudio.x;                         // pulsiert mit dem Bass

    float d = length((fragCoord - iMouse.xy) / iResolution.y);
    col += iMouse.w * 0.15 / (1.0 + d * d * 200.0);      // Licht an der Maus
    return float4(col * 0.6, 1.0);
}
)HLSL";

static const char* kReadme = R"(Lumen – Bibliothek
==================

Alles, was in diesem Ordner liegt, erscheint im Lumen-Menü (Rechtsklick auf das Tray-Symbol).

1) Einfach Dateien hineinlegen
   Bilder: jpg, png, bmp, webp, gif (erstes Bild)
   Videos: mp4, webm, mkv, mov, wmv, avi (was Windows abspielen kann, ohne Ton)
   Shader: .hlsl-Dateien mit einer Funktion mainImage

2) Ordner mit wallpaper.ini (für Effekte auf Bild/Video oder eigene Namen)
   name=Mein Hintergrund
   source=video.mp4          (optional, Bild oder Video im selben Ordner)
   shader=shader.hlsl        (optional, eigener Effekt; ohne: Bild/Video wird eingepasst)
   fit=cover                 (cover | contain | stretch)
   fps=30                    (optional, Obergrenze für diesen Hintergrund)
   speed=1.0                 (Wiedergabetempo bei Videos)

Unterordner werden im Menü als Gruppen angezeigt.
Shader-Dateien werden beim Speichern automatisch neu geladen.
)";

const std::vector<ExampleWallpaper>& Examples() {
    static const std::vector<ExampleWallpaper> v = {
        {L"Aurora", kAuroraIni, kAurora},
        {L"Punktfeld", kDotsIni, kDots},
        {L"Spektrum", kSpectrumIni, kSpectrum},
        {L"Tag und Nacht", kDayNightIni, kDayNight},
    };
    return v;
}

const char* ShaderTemplate() { return kTemplate; }
const char* LibraryReadme() { return kReadme; }
