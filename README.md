# Lumen – Live-Hintergründe, die deinen PC in Ruhe lassen

Lumen zeigt **Shader, Videos und Bilder hinter deinen Desktop-Symbolen** – wie Wallpaper Engine, aber so schlank wie Kumo:
ein einziges Programm (~1,3 MB), kein Browser, kein Konto, keine Telemetrie, keine Adminrechte.

- **Direct3D 11 direkt** – ein Gerät, ein Prozess für alle Bildschirme
- **Videos per Hardware-Dekodierung** (Media Foundation), Ton wird gar nicht erst dekodiert
- **Interaktiv**: Shader kennen Mausposition und Klicks auf den Desktop
- **Audio-reaktiv**: 16 Frequenzbänder aus dem, was gerade läuft (WASAPI-Loopback) – der Audio-Thread läuft nur, wenn ein Hintergrund ihn braucht
- **Bibliothek** mit Gruppen, „Datei hinzufügen", eigene Shader mit Live-Neuladen
- **Online-Katalog** (selbst hostbar, z. B. auf GitHub) und **Import der Video-Hintergründe aus Wallpaper Engine**
- **Pausiert von selbst**: verdeckter Bildschirm (Spiel, maximiertes Fenster), Akku/Energiesparen, gesperrter PC, Remote Desktop, Bildschirm aus
- **Sparsam**: Standbilder werden einmal gezeichnet und danach freigegeben, Bildrate 15/30/60/144, Renderauflösung 50–100 %, niedrige CPU- und GPU-Priorität

## Starten

1. `Lumen.exe` irgendwo hinlegen (z. B. `C:\Programme\Lumen` oder `%LOCALAPPDATA%\Lumen`) und starten.
2. Windows-SmartScreen warnt, weil das Programm nicht signiert ist: **Weitere Informationen → Trotzdem ausführen**.
3. Vorher Wallpaper Engine oder Lively beenden – zwei Hintergrund-Programme stören sich.
4. **Rechtsklick auf das Lumen-Symbol** in der Taskleiste (ggf. unter `^`) öffnet das Menü.

Zum Beenden: Menü → *Beenden*. Dein normaler Windows-Hintergrund kommt zurück.

## Das Menü

| Eintrag | Was es tut |
|---|---|
| Hintergrund / Bildschirm 1, 2 … | Hintergrund für alle oder pro Bildschirm wählen, auch „Kein Live-Hintergrund" |
| Bild oder Video hinzufügen… | Datei wird in die Bibliothek kopiert und sofort angezeigt |
| Eigenen Shader erstellen… | legt eine Vorlage an und öffnet sie im Editor – **speichern genügt**, Lumen lädt neu |
| Bibliothek öffnen | Explorer im Bibliotheksordner |
| Online-Bibliothek | Katalog laden, Einträge mit einem Klick installieren |
| Einstellungen | Bildrate, Renderauflösung, Akku-Pause, Musik-Reaktion, Wallpaper-Engine-Videos, Autostart |
| Pausieren | alles anhalten |

Tipp: `Lumen.exe "C:\pfad\video.mp4"` (oder „Öffnen mit → Lumen") fügt die Datei hinzu und zeigt sie an.

## Bibliothek

Ordner: `%LOCALAPPDATA%\Lumen\Bibliothek` – alles darin erscheint im Menü, Unterordner werden zu Gruppen.

- **Einfach Dateien hineinlegen:** jpg, png, bmp, webp, gif (erstes Bild), mp4, webm, mkv, mov, wmv, avi, `.hlsl`
- **Ordner mit `wallpaper.ini`** für Effekte auf Bild/Video oder eigene Namen:

```ini
name=Mein Hintergrund
source=video.mp4      ; optional: Bild oder Video
shader=shader.hlsl    ; optional: eigener Effekt (sonst wird source nur eingepasst)
fit=cover             ; cover | contain | stretch
fps=30                ; optional: Obergrenze für diesen Hintergrund
speed=1.0             ; Wiedergabetempo bei Videos
```

## Eigene Shader

Ein Shader ist HLSL mit einer Funktion `mainImage` – ähnlich wie auf shadertoy.com:

```hlsl
float4 mainImage(float2 fragCoord) {
    float2 uv = fragCoord / iResolution;
    float3 col = 0.5 + 0.5 * cos(iTime + uv.xyx * 3.0 + float3(0, 2, 4));
    col *= 0.6 + 0.6 * iAudio.x;                                     // Bass
    col += iMouse.w * 0.2 / (1.0 + length(fragCoord - iMouse.xy) * 0.02);   // Maus
    return float4(col, 1);
}
```

| Wert | Bedeutung |
|---|---|
| `iResolution` | Größe in Pixeln (`float2`) |
| `iTime`, `iTimeDelta`, `iFrame` | Zeit in Sekunden, Abstand zum letzten Bild, Bildzähler |
| `iMouse` | `xy` Maus in Pixeln (oben links = 0,0), `z` Taste gedrückt, `w` Maus über dem Desktop (1/0) |
| `iClick` | `xy` letzter Klick auf den Desktop, `z` Sekunden seit dem Klick, `w` Anzahl Klicks |
| `iAudio` | `x` Bass, `y` Mitten, `z` Höhen, `w` Pegel – alles 0..1 |
| `band(i)`, `bandf(x)` | Frequenzband 0..15 bzw. interpoliert über 0..1 |
| `iHour` | lokale Uhrzeit 0..24 (Tag-und-Nacht-Hintergründe) |
| `iChannel0`, `iChannelResolution`, `texFit(fragCoord)` | Bild/Video aus `source=`, passend eingepasst |
| `L_hash`, `L_noise`, `L_fbm`, `L_hue` | Hilfsfunktionen |

Shadertoy-Code übertragen: `vec2→float2`, `mix→lerp`, `fract→frac`, `mod→fmod`, `texture→iChannel0.Sample(iSampler, uv)`.
Achtung: Hier ist **y = 0 oben**. Fehler zeigt Lumen als Hinweis an (Details in `lumen.log`), der Hintergrund wird dann rot gestreift.

Lumen merkt automatisch, ob ein Shader Zeit, Maus oder Audio nutzt. Ohne `iTime`/`iMouse`/`iAudio` wird er nur einmal gezeichnet und kostet danach nichts.

## Online-Katalog (dein eigener „Workshop")

Ein Katalog ist eine Textdatei, eine Zeile pro Hintergrund:

```
Name | https://…/datei.zip | Beschreibung
```

Erlaubt sind `.zip` (Ordner mit `wallpaper.ini`), Bilder, Videos und `.hlsl` – nur über `https://`.
Im Ordner `catalog/` liegt eine Vorlage samt Beispielpaket „Lavalampe":

1. Ordner `catalog/` als eigenes GitHub-Repository hochladen (z. B. `lumen-katalog`).
2. In `katalog.txt` `DEINNAME` ersetzen.
3. In Lumen: Menü → Online-Bibliothek → *Katalog-Adresse festlegen…* →
   `catalog_url=https://raw.githubusercontent.com/DEINNAME/lumen-katalog/main/katalog.txt`
4. Menü → Einstellungen → *Neu laden*, dann Online-Bibliothek → *Katalog laden*.

Zips werden mit dem in Windows eingebauten `tar.exe` entpackt. Hintergründe enthalten nie ausführbaren Code – Shader laufen nur auf der Grafikkarte.

## Wallpaper Engine

Besitzt du Wallpaper Engine, zeigt Lumen deine abonnierten **Video**-Hintergründe aus dem Steam-Workshop unter der Gruppe „Wallpaper Engine" an (Dateien werden nur gelesen, nicht kopiert). Szenen- und Web-Hintergründe nutzen eigene Formate von Wallpaper Engine und werden nicht unterstützt.

## Einstellungen (`%APPDATA%\Lumen\config.ini`)

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `fps` | 30 | Bildrate-Obergrenze |
| `render_scale` | 1.0 | 0.25–1.0, Rest skaliert die Grafikkarte kostenlos hoch |
| `pause_on_battery` | 1 | Pause im Akkubetrieb und Energiesparmodus |
| `pause_remote` | 1 | Pause bei Remote Desktop |
| `audio` | 1 | Audio-Reaktion erlauben |
| `wallpaper_engine` | 1 | Wallpaper-Engine-Videos anzeigen |
| `catalog_url` | – | Adresse des Online-Katalogs |
| `desktop_layered` | 0 | Hilfe für Windows 11 24H2, falls der Hintergrund unsichtbar bleibt |
| `legacy_swapchain` | 0 | älteres Darstellungsverfahren, falls schwarz/unsichtbar |
| `debug_hour` | – | Uhrzeit für `iHour` festlegen (zum Testen) |

## Fehlersuche

- **Nichts zu sehen / schwarzer Desktop:** Menü → Einstellungsdatei öffnen, `desktop_layered=1` eintragen, speichern, *Neu laden*. Hilft das nicht, zusätzlich `legacy_swapchain=1`. Bitte `lumen.log` (gleicher Ordner) beilegen, wenn du einen Fehler meldest.
- **Video startet nicht:** Codec fehlt (z. B. HEVC-Erweiterung aus dem Store) oder Windows-„N"-Edition ohne Media Feature Pack.
- **Tray-Symbol fehlt:** unter `^` in der Taskleiste suchen und herausziehen.

## Selbst bauen

- **Linux/WSL oder MSYS2 (MinGW-w64):** `make` → `build/Lumen.exe` (unter MSYS2: `make CROSS=`)
- **Visual Studio 2022:** `cmake -B build -A x64` und `cmake --build build --config Release`
- **GitHub:** Die Action in `.github/workflows/build.yml` baut die exe bei jedem Push.

Aufbau: `main.cpp` (Tray, Bildschirme, Schleife, Pause-Logik) · `renderer.cpp` (D3D11, Shader, Bild, Video) · `desktop.cpp` (Einbettung hinter die Symbole, Verdeckung) · `audio.cpp` (Loopback + FFT) · `library.cpp` (Bibliothek, Katalog, Wallpaper Engine) · `examples.cpp` (mitgelieferte Shader).
