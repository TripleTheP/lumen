# Lumen – Projektkontext für Claude

Schlanke Live-Hintergründe für Windows (Vorbild: Kumo, kumo.hyos.tech). Ziel: Funktionen ähnlich Wallpaper Engine,
aber minimaler Ressourcenverbrauch. C++17, Win32, Direct3D 11, keine externen Bibliotheken.

## Sprache
- Antworten, Kommentare, UI-Texte und Commit-Nachrichten auf **Deutsch**.
- Befehle immer als Codeblock.

## Aufbau
- `src/main.cpp` – App: Tray-Menü, Bildschirme, Hauptschleife (Waitable Timer, fps-Begrenzung), Auto-Pause, Katalog-Threads
- `src/renderer.cpp` – D3D11-Gerät, Shader-Prelude (`mainImage`-API), Shader-Cache (`%LOCALAPPDATA%\Lumen\Cache`),
  Animierte GIFs (`GifSource`: WIC-Einzelbilder auf einer Leinwand, `Source::NextChange()` lässt den Hauptloop zwischen Bildern ruhen),
  Eigenschaften (`// @param` im Shader → `#define` auf `lumenParams[i]`, Werte aus `param.*` der wallpaper.ini, Neuladen bei ini-Änderung),
  Bilder (WIC), Videos (Media Foundation Source Reader; RGB32 → NV12-GPU → NV12-Staging als Fallbacks; MF wird dynamisch geladen)
- `src/desktop.cpp` – Einbettung hinter die Desktop-Symbole (WorkerW klassisch, Win11 24H2 unter Progman, Ersatzmodus ohne Explorer), Verdeckungsprüfung
- `src/audio.cpp` – WASAPI-Loopback + FFT → 16 Bänder; Thread läuft nur, wenn ein Shader Audio nutzt
- `src/library.cpp` – Bibliothek (`%LOCALAPPDATA%\Lumen\Bibliothek`), wallpaper.ini, Wallpaper-Engine-Videoimport, Online-Katalog (URLDownloadToFile + tar.exe)
- `src/settings.cpp` – Einstellungsfenster (Raster mit Vorschaubildern, Reiter „Eigenschaften“ und „Einstellungen“); es ändert nichts selbst:
  alle Schalter laufen über `SettingsContext::command` in dieselben Menübefehle (`commands.h`, `App::OnCommand`, `App::MenuChecked`), damit
  Tray-Menü und Fenster nicht auseinanderlaufen. Eigenschaften: beim Ziehen nur `Screen::pv` setzen, erst beim Loslassen `SetIniValue` + `iniTime` nachziehen
  (sonst lädt die Dateiüberwachung neu und startet die Animation neu). Auswahl nur bei Klick (`NM_CLICK`), nie bei `LVN_ITEMCHANGED`
  (sonst würde schon das Öffnen `SetAll` auslösen). Vorschau: `MakeWallpaperThumbnail`, Shader mit ihren Eigenschaften-Vorgaben.
- `src/schedule.cpp` – Zeitplan (`%APPDATA%\Lumen\zeitplan.txt`, `Uhrzeit | Hintergrund`); `main.cpp` (`UpdateSchedule`, 1×/s)
  wendet den aktuellen Abschnitt an, ohne `config.ini` zu ändern (`scheduled_`, `scheduleHeld_`); `debug_hour` gilt auch hier
- `src/examples.cpp` – mitgelieferte Shader (Aurora, Punktfeld, Spektrum, Tag und Nacht) und Vorlage.
  **Wer einen Beispiel-Shader ändert, hängt `Fnv1a` der bisherigen Fassung an `replaces` an** (`make test` gibt sie je
  Beispiel aus, vor der Änderung ablesen) – sonst bekommen bestehende Installationen die neue Fassung nie
  (unveränderte Kopien werden beim Start ersetzt, bearbeitete bleiben).
  Aurora hat Eigenschaften (`// @param`); aktuelle Fassung: Hash aus `make test` bei der nächsten Änderung eintragen.
- `tests/selftest.cpp` – `make test` (nur Windows): Katalog-Installation, Vollbild-Erkennung, Beispiel-Shader, Panorama-Kacheln,
  ini/Eigenschaften, Zeitplan, GIF (WIC-erzeugt und von Hand gebaut mit Teilbildern/Entsorgung)
- `catalog/` – Vorlage für einen selbst gehosteten Online-Katalog
- Einstellungen: `%APPDATA%\Lumen\config.ini`, Log: `%APPDATA%\Lumen\lumen.log`

## Bauen
- MinGW (Linux/WSL): `make` → `build/Lumen.exe`; MSYS2: `make CROSS=`; Debug: `make DEBUG=1` → `build/debug/Lumen.exe`
- VS Code: `.vscode/` mit Tasks (Strg+Umschalt+B), gdb-Debugging (F5) und IntelliSense für MSYS2 UCRT64 in `C:\msys64`.
  Die Tasks beenden eine laufende Instanz vorher per `taskkill` (WM_CLOSE → sauberes Beenden).
- Visual Studio: `cmake -B build -A x64` und `cmake --build build --config Release` (CMake/MSVC noch nicht getestet)
- GitHub Action `.github/workflows/build.yml` baut bei jedem Push auf `main` und jedem PR; ein Tag `vX.Y.Z` erzeugt ein Release mit der exe
- Repo-Dateien: `LICENSE` (MIT), `CHANGELOG.md` (bei jeder Änderung unter „Unveröffentlicht" ergänzen), `CONTRIBUTING.md`, `SECURITY.md`, Issue-/PR-Vorlagen in `.github/`
  (Private Schwachstellenmeldung gibt es erst, wenn das Repo öffentlich ist: dann unter Settings → Security aktivieren
  und in `SECURITY.md` sowie `.github/ISSUE_TEMPLATE/config.yml` den Link `…/security/advisories/new` eintragen.)

## Test-Stand (0.1.0)
Getestet unter Wine (Software-Rendering): Start, alle Shader, Bild, Video inkl. Schleife, Shader-Fehleranzeige, Einzelinstanz/Datei übergeben.
Auf echtem Windows 11 26H2 (Build 26300) getestet: Einbettung hinter die Symbole (Modus 24H2) auf 3 Bildschirmen
3840×2160 bei 150 %, Aurora animiert, Verdeckungs-Pause pro Bildschirm, sauberes Beenden per `taskkill`.
Vollbild-Pause (GPU-Last → 0 %), Katalog-Installation aus lokaler Datei (Selbsttest).
GPU-Last (RTX 4070 Ti SUPER, 4K, 30 fps, Taskmanager-3D): Aurora 12 % → 2,1 % durch `shader_res=1080` und
Aurora-Optimierung (überspringt verdeckte Bereiche; Bild pixelgenau gleich, mit GPU-Zeitstempeln gemessen 1,42 → 0,78 ms/Bild in 4K).
Panorama (`span=1`): Selbsttest beweist, dass Kacheln pixelgleich das Gesamtbild ergeben (auch bei halber Renderauflösung);
Aufbau: `lumenMap` im cbuffer bildet jeden Bildschirm in den Desktop-Pixelraum ab (`fragCoord = Pixel * zw + xy`), kein gemeinsamer Skalierungsfaktor nötig.
**Noch nicht auf echtem Windows getestet:** Panorama sichtbar auf den 3 Bildschirmen, Audio-Reaktion sichtbar, Maus/Klick, Video, Katalog-Download über https.
Diagnose-Schalter: `desktop_layered=1`, `legacy_swapchain=1`.
Hinweis: GDI-Screenshots (CopyFromScreen) zeigen den Desktop-Bereich hier schwarz – Sichtprüfung des Hintergrunds braucht einen Menschen.
Normale Fenster (Einstellungsfenster) lassen sich dagegen prüfen: Fenster kurz `HWND_TOPMOST` setzen und nur dessen Rechteck aufnehmen
(sonst landet fremder Inhalt im Bild, auch Privates); bedient wird per synchronen Fensternachrichten (`SendMessage`, nicht `PostMessage` –
Tab-Steuerelemente ignorieren gepostete Klicks). In PowerShell-P/Invoke `FindWindow…` mit `[NullString]::Value` statt `$null` aufrufen und
`CharSet=Unicode` angeben.

## Grundsätze
- Nichts im Hintergrund laufen lassen, was gerade keiner sieht (pausieren, Speicher freigeben).
- Keine neuen Abhängigkeiten ohne guten Grund; alles mit Windows-Bordmitteln.
- Hintergründe enthalten nie ausführbaren Code – nur Medien und HLSL.
