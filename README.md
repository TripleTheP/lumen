# Lumen – Live-Hintergründe, die deinen PC in Ruhe lassen

[![Build](https://github.com/TripleTheP/lumen/actions/workflows/build.yml/badge.svg)](https://github.com/TripleTheP/lumen/actions/workflows/build.yml)
[![Lizenz: MIT](https://img.shields.io/badge/Lizenz-MIT-blue.svg)](LICENSE)
![Plattform: Windows](https://img.shields.io/badge/Plattform-Windows-0078D4)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C)

Lumen zeigt **Shader, Videos und Bilder hinter deinen Desktop-Symbolen** – wie Wallpaper Engine, aber so schlank wie Kumo:
ein einziges Programm (~1,4 MB), kein Browser, kein Konto, keine Telemetrie, keine Adminrechte.
Geschrieben in C++17 mit Win32 und Direct3D 11, ganz ohne externe Bibliotheken.

- **Direct3D 11 direkt** – ein Gerät, ein Prozess für alle Bildschirme
- **Videos per Hardware-Dekodierung** (Media Foundation), Ton wird gar nicht erst dekodiert
- **Interaktiv**: Shader kennen Mausposition und Klicks auf den Desktop
- **Audio-reaktiv**: 16 Frequenzbänder aus dem, was gerade läuft (WASAPI-Loopback) – der Audio-Thread läuft nur, wenn ein Hintergrund ihn braucht
- **Bibliothek** mit Gruppen, „Datei hinzufügen", eigene Shader mit Live-Neuladen
- **Online-Katalog** (selbst hostbar, z. B. auf GitHub) und **Import der Video-Hintergründe aus Wallpaper Engine**
- **Pausiert von selbst**: verdeckter Bildschirm (maximiertes Fenster), Vollbild-Spiel oder -Video (dann alle Bildschirme), Akku/Energiesparen, gesperrter PC, Remote Desktop, Bildschirm aus
- **Sparsam**: Standbilder werden einmal gezeichnet und danach freigegeben, Shader werden höchstens in 1080p berechnet und hochskaliert (auf 4K ein Viertel der Arbeit), Bildrate 15/30/60/144, Renderauflösung 50–100 %, niedrige CPU- und GPU-Priorität

## Voraussetzungen

- Windows 11 (64 Bit) mit einer Grafikkarte, die Direct3D 11 kann. Getestet auf Windows 11 26H2; Windows 10 sollte laufen, ist aber nicht getestet.
- Für Videos muss der Codec in Windows vorhanden sein: HEVC braucht die Erweiterung aus dem Microsoft Store, „N"-Editionen das Media Feature Pack.

## Starten

1. `Lumen.exe` aus den [Releases](../../releases/latest) laden (oder [selbst bauen](#selbst-bauen)), irgendwo hinlegen (z. B. `C:\Programme\Lumen` oder `%LOCALAPPDATA%\Lumen`) und starten.
2. Windows-SmartScreen warnt, weil das Programm nicht signiert ist: **Weitere Informationen → Trotzdem ausführen**.
3. Vorher Wallpaper Engine oder Lively beenden – zwei Hintergrund-Programme stören sich.
4. **Linksklick auf das Lumen-Symbol** in der Taskleiste (ggf. unter `^`) öffnet das Fenster mit Vorschaubildern, **Rechtsklick** das Menü.

Zum Beenden: Menü → *Beenden*. Dein normaler Windows-Hintergrund kommt zurück.

## Das Einstellungsfenster

Linksklick auf das Tray-Symbol (oder Menü → *Hintergründe und Einstellungen…*). Links liegen alle Hintergründe als **Raster mit
Vorschaubildern**, nach Ordnern gruppiert: ein Klick wählt den Hintergrund. Die Bilder entstehen beim Öffnen nacheinander (Shader werden
kurz gezeichnet, Bilder skaliert, Videos kommen aus der Windows-Vorschau), das Fenster bleibt dabei bedienbar. Mit mehreren Bildschirmen
wählst du oben, ob der Hintergrund für alle oder nur für einen gelten soll.

Rechts zwei Reiter:
- **Eigenschaften:** Regler und Farbfelder für die einstellbaren Werte des gewählten Hintergrunds (siehe [Einstellbare Werte](#einstellbare-werte-eigenschaften)).
  Beim Ziehen eines Reglers siehst du die Wirkung sofort auf dem Desktop, gespeichert wird beim Loslassen.
- **Einstellungen:** alle Schalter des Menüs, Bildrate, Renderauflösung und Knöpfe für Zeitplan, Bibliothek und neue Hintergründe.

Das Fenster ändert nichts anderes als das Menü: beides führt dieselben Befehle aus. Schließt du es, gibt Lumen den Speicher wieder frei.
Es passt sich der Bildschirmskalierung an (auch bei unterschiedlicher Skalierung pro Bildschirm). Bewusst weggelassen: Suche,
Ziehen und Ablegen, Vorschaubilder für Wallpaper-Engine-Szenen.

## Das Menü

| Eintrag | Was es tut |
|---|---|
| Hintergründe und Einstellungen… | öffnet das Einstellungsfenster (macht auch der Linksklick auf das Symbol) |
| Hintergrund / Bildschirm 1, 2 … | Hintergrund für alle oder pro Bildschirm wählen, auch „Kein Live-Hintergrund" |
| Bild oder Video hinzufügen… | Datei wird in die Bibliothek kopiert und sofort angezeigt |
| Eigenen Shader erstellen… | legt eine Vorlage an und öffnet sie im Editor – **speichern genügt**, Lumen lädt neu |
| Eigenschaften von „…“ ändern… | erscheint, wenn der Hintergrund einstellbare Werte hat (Tempo, Farben …); öffnet die `wallpaper.ini` |
| Bibliothek öffnen | Explorer im Bibliotheksordner |
| Online-Bibliothek | Katalog laden, Einträge mit einem Klick installieren |
| Einstellungen | Bildrate, Renderauflösung, Akku-Pause, Vollbild-Pause, Panorama (ab 2 Bildschirmen), Zeitplan, Musik-Reaktion, Wallpaper-Engine-Videos, Autostart |
| Pausieren | alles anhalten |

Tipp: `Lumen.exe "C:\pfad\video.mp4"` (oder „Öffnen mit → Lumen") fügt die Datei hinzu und zeigt sie an.

### Zeitplan: Hintergrund nach Tageszeit

Menü → Einstellungen → *Zeitplan: Hintergrund nach Tageszeit* einschalten und *Zeitplan bearbeiten…* wählen. Lumen legt die Datei
`%APPDATA%\Lumen\zeitplan.txt` an (samt Liste deiner Hintergründe zum Abschreiben) und öffnet sie. Pro Wechsel eine Zeile:

```
06:00 | Beispiele\Aurora
18:00 | Beispiele\Spektrum
23:30 | none
```

Jeder Hintergrund gilt ab seiner Uhrzeit bis zur nächsten Zeile, auch über Mitternacht (im Beispiel läuft ab 23:30 kein
Live-Hintergrund, `none`, bis 06:00). Speichern genügt, Lumen lädt die Datei sofort neu. Beim Start gilt gleich der Abschnitt
der aktuellen Uhrzeit. Wählst du selbst einen Hintergrund, bleibt er bis zum nächsten Wechsel des Zeitplans. Einträge mit unbekanntem
Hintergrund werden übersprungen. Der Zeitplan gilt für alle Bildschirme; deine gewählten Hintergründe bleiben in den Einstellungen
erhalten und kommen zurück, wenn du den Zeitplan ausschaltest.

### Panorama über mehrere Bildschirme

Menü → Einstellungen → *Ein Hintergrund über alle Bildschirme (Panorama)*: Statt auf jedem Bildschirm dasselbe zu zeigen,
legt Lumen **einen** Hintergrund über den ganzen Desktop. Maßgeblich ist die Anordnung unter *Windows-Einstellungen → Anzeige*:
Lumen nimmt das kleinste Rechteck um alle Bildschirme. Stehen die Bildschirme versetzt oder übereinander, bleiben also
leere Ecken, und ein Bild im Modus `cover` wird auf dieses Rechteck zugeschnitten (wie bei „Spanning" in Windows).
Im Panorama gilt eine gemeinsame Auswahl; die Auswahl pro Bildschirm bleibt gespeichert und kommt zurück, wenn du es ausschaltest.

## Bibliothek

Ordner: `%LOCALAPPDATA%\Lumen\Bibliothek` – alles darin erscheint im Menü, Unterordner werden zu Gruppen.

- **Einfach Dateien hineinlegen:** jpg, png, bmp, webp, gif (auch animiert), mp4, webm, mkv, mov, wmv, avi, `.hlsl`
- **Ordner mit `wallpaper.ini`** für Effekte auf Bild/Video oder eigene Namen:

```ini
name=Mein Hintergrund
source=video.mp4      ; optional: Bild oder Video
shader=shader.hlsl    ; optional: eigener Effekt (sonst wird source nur eingepasst)
fit=cover             ; cover | contain | stretch
fps=30                ; optional: Obergrenze für diesen Hintergrund
speed=1.0             ; Wiedergabetempo bei Videos
param.tempo=2.0       ; Eigenschaft des Shaders (siehe unten)
```

Kommentare mit `;` hinter einem Wert sind erlaubt (mit Leerzeichen davor), Zeilen mit `#` oder `;` am Anfang ebenfalls.
Die Datei wird beim Speichern sofort neu geladen.

**Animierte GIFs** laufen wie Videos, aber noch sparsamer: Lumen zeichnet nur, wenn ein neues GIF-Bild fällig ist
(ein GIF mit 10 Bildern pro Sekunde kostet also 10 Zeichenvorgänge pro Sekunde, nicht 30). Die Dauer jedes Bildes stammt aus
der GIF-Datei, `speed=` in `wallpaper.ini` ändert das Tempo, `fit=` wie bei Bildern. GIFs über 4096 Pixel Kantenlänge zeigt Lumen
als Standbild. Kleine GIFs wirken skaliert unscharf; für große Flächen sind Videos die bessere Wahl.

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
| `iTime`, `iTimeDelta`, `iFrame` | Zeit in Sekunden (beginnt jede Stunde wieder bei 0, damit Animationen auch nach Tagen flüssig bleiben), Abstand zum letzten Bild, Bildzähler |
| `iMouse` | `xy` Maus in Pixeln (oben links = 0,0), `z` Taste gedrückt, `w` Maus über dem Desktop (1/0) |
| `iClick` | `xy` letzter Klick auf den Desktop, `z` Sekunden seit dem Klick, `w` Anzahl Klicks |
| `iAudio` | `x` Bass, `y` Mitten, `z` Höhen, `w` Pegel – alles 0..1 |
| `band(i)`, `bandf(x)` | Frequenzband 0..15 bzw. interpoliert über 0..1 |
| `iHour` | lokale Uhrzeit 0..24 (Tag-und-Nacht-Hintergründe) |
| `iChannel0`, `iChannelResolution`, `texFit(fragCoord)` | Bild/Video aus `source=`, passend eingepasst |
| `L_hash`, `L_noise`, `L_fbm`, `L_hue` | Hilfsfunktionen |

Shadertoy-Code übertragen: `vec2→float2`, `mix→lerp`, `fract→frac`, `mod→fmod`, `texture→iChannel0.Sample(iSampler, uv)`.
Achtung: Hier ist **y = 0 oben**. Fehler zeigt Lumen als Hinweis an (Details in `lumen.log`), der Hintergrund wird dann rot gestreift.

Im Panorama-Modus (siehe oben) beziehen sich `iResolution`, `fragCoord`, `iMouse` und `iClick` auf den **ganzen Desktop** in physischen Pixeln, nicht auf den einzelnen Bildschirm. Shader, die nur diese Werte benutzen, laufen ohne Änderung.

### Einstellbare Werte (Eigenschaften)

Ein Shader kann Werte anbieten, die man in `wallpaper.ini` ändert, ohne den Shader zu bearbeiten. Er deklariert sie in
Kommentaren und benutzt sie dann wie Variablen:

```hlsl
// @param tempo float 1.0 0.2 3.0 Geschwindigkeit der Lichter    (Vorgabe, kleinster, größter Wert, Beschriftung)
// @param farbe color #19FF8C Farbe der Vorhänge
float4 mainImage(float2 fragCoord) {
    return float4(farbe * (0.5 + 0.5 * sin(iTime * tempo)), 1);
}
```

In `wallpaper.ini` stehen die Werte als `param.tempo=2.0` und `param.farbe=#FF8800`; ohne Eintrag gilt die Vorgabe.
Zahlen dürfen mit Komma geschrieben werden, außerhalb von Kleinst- und Größtwert werden sie begrenzt, ungültige Werte
ignoriert Lumen. Am bequemsten: Menü → *Eigenschaften von „…“ ändern…* trägt alle Werte mit Vorgabe in die Datei ein und
öffnet sie; **speichern genügt**. Es sind bis zu 16 Werte pro Shader möglich. Die mitgelieferte *Aurora* zeigt es
(Tempo, Helligkeit, zwei Farben). Eigenschaften gibt es nur bei Hintergründen mit `wallpaper.ini`, nicht bei losen `.hlsl`-Dateien.

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
| `shader_res` | 1080 | Reine Shader höchstens mit so vielen Pixeln wie 1080p berechnen (Bilder und Videos bleiben scharf); `0` = volle Auflösung |
| `pause_on_battery` | 1 | Pause im Akkubetrieb und Energiesparmodus |
| `pause_remote` | 1 | Pause bei Remote Desktop |
| `span` | 0 | `1` = ein Hintergrund über alle Bildschirme (Panorama), siehe oben |
| `pause_fullscreen` | 1 | Alle Bildschirme pausieren, solange ein Vollbild-Programm (Spiel, Video) im Vordergrund ist |
| `audio` | 1 | Audio-Reaktion erlauben |
| `wallpaper_engine` | 1 | Wallpaper-Engine-Videos anzeigen |
| `catalog_url` | – | Adresse des Online-Katalogs |
| `desktop_layered` | 0 | Hilfe für Windows 11 24H2, falls der Hintergrund unsichtbar bleibt |
| `legacy_swapchain` | 0 | älteres Darstellungsverfahren, falls schwarz/unsichtbar |
| `schedule` | 0 | `1` = Zeitplan (`zeitplan.txt`) einschalten, siehe oben |
| `debug_hour` | – | Uhrzeit für `iHour` und den Zeitplan festlegen (zum Testen, z. B. `18.5`) |

## Fehlersuche

- **Nichts zu sehen / schwarzer Desktop:** Menü → Einstellungsdatei öffnen, `desktop_layered=1` eintragen, speichern, *Neu laden*. Hilft das nicht, zusätzlich `legacy_swapchain=1`. Bitte `lumen.log` (gleicher Ordner) beilegen, wenn du einen Fehler meldest.
- **Video startet nicht:** Codec fehlt (z. B. HEVC-Erweiterung aus dem Store) oder Windows-„N"-Edition ohne Media Feature Pack.
- **Tray-Symbol fehlt:** unter `^` in der Taskleiste suchen und herausziehen.

## Selbst bauen

- **Linux/WSL oder MSYS2 (MinGW-w64):** `make` → `build/Lumen.exe` (unter MSYS2: `make CROSS=`), mit Debug-Symbolen: `make DEBUG=1` → `build/debug/Lumen.exe`, Selbsttest: `make test` (nur unter Windows)
- **VS Code (MSYS2 in `C:\msys64` mit `mingw-w64-ucrt-x86_64-gcc`, `make`, `mingw-w64-ucrt-x86_64-gdb`):** Strg+Umschalt+B baut, F5 startet den Debugger. Eine laufende Lumen-Instanz wird vorher beendet.
- **Visual Studio 2022:** `cmake -B build -A x64` und `cmake --build build --config Release`
- **GitHub:** Die Action in `.github/workflows/build.yml` baut die exe bei jedem Push auf `main` und jedem Pull Request. Ein Tag wie `v0.2.0` erzeugt automatisch ein Release mit der exe.

Aufbau: `main.cpp` (Tray, Bildschirme, Schleife, Pause-Logik) · `renderer.cpp` (D3D11, Shader, Bild, Video) · `desktop.cpp` (Einbettung hinter die Symbole, Verdeckung) · `audio.cpp` (Loopback + FFT) · `library.cpp` (Bibliothek, Katalog, Wallpaper Engine) · `examples.cpp` (mitgelieferte Shader).

## Mitmachen

Fehler, Ideen und Pull Requests sind willkommen – siehe [CONTRIBUTING.md](CONTRIBUTING.md). Änderungen stehen im [CHANGELOG](CHANGELOG.md), Hinweise zur Sicherheit in [SECURITY.md](SECURITY.md).

## Lizenz

[MIT](LICENSE) © 2026 TripleTheP
