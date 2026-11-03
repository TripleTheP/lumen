# Lumen – Projektkontext für Claude

Schlanke Live-Hintergründe für Windows (Vorbild: Kumo, kumo.hyos.tech). Ziel: Funktionen ähnlich Wallpaper Engine,
aber minimaler Ressourcenverbrauch. C++17, Win32, Direct3D 11, keine externen Bibliotheken.

## Sprache
- Antworten, Kommentare, UI-Texte und Commit-Nachrichten auf **Deutsch**.
- Befehle immer als Codeblock.

## Aufbau
- `src/main.cpp` – App: Tray-Menü, Bildschirme, Hauptschleife (Waitable Timer, fps-Begrenzung), Auto-Pause, Katalog-Threads
- `src/renderer.cpp` – D3D11-Gerät, Shader-Prelude (`mainImage`-API), Shader-Cache (`%LOCALAPPDATA%\Lumen\Cache`),
  Bilder (WIC), Videos (Media Foundation Source Reader; RGB32 → NV12-GPU → NV12-Staging als Fallbacks; MF wird dynamisch geladen)
- `src/desktop.cpp` – Einbettung hinter die Desktop-Symbole (WorkerW klassisch, Win11 24H2 unter Progman, Ersatzmodus ohne Explorer), Verdeckungsprüfung
- `src/audio.cpp` – WASAPI-Loopback + FFT → 16 Bänder; Thread läuft nur, wenn ein Shader Audio nutzt
- `src/library.cpp` – Bibliothek (`%LOCALAPPDATA%\Lumen\Bibliothek`), wallpaper.ini, Wallpaper-Engine-Videoimport, Online-Katalog (URLDownloadToFile + tar.exe)
- `src/examples.cpp` – mitgelieferte Shader (Aurora, Punktfeld, Spektrum, Tag und Nacht) und Vorlage.
  **Wer einen Beispiel-Shader ändert, setzt `replaces` auf `Fnv1a` der bisherigen Fassung** – sonst bekommen bestehende
  Installationen die neue Fassung nie (unveränderte Kopien werden beim Start ersetzt, bearbeitete bleiben).
- `tests/selftest.cpp` – `make test` (nur Windows): Katalog-Installation, Vollbild-Erkennung, alle Beispiel-Shader kompilieren
- `catalog/` – Vorlage für einen selbst gehosteten Online-Katalog
- Einstellungen: `%APPDATA%\Lumen\config.ini`, Log: `%APPDATA%\Lumen\lumen.log`

## Bauen
- MinGW (Linux/WSL): `make` → `build/Lumen.exe`; MSYS2: `make CROSS=`; Debug: `make DEBUG=1` → `build/debug/Lumen.exe`
- VS Code: `.vscode/` mit Tasks (Strg+Umschalt+B), gdb-Debugging (F5) und IntelliSense für MSYS2 UCRT64 in `C:\msys64`.
  Die Tasks beenden eine laufende Instanz vorher per `taskkill` (WM_CLOSE → sauberes Beenden).
- Visual Studio: `cmake -B build -A x64` und `cmake --build build --config Release` (CMake/MSVC noch nicht getestet)
- GitHub Action `.github/workflows/build.yml` baut bei jedem Push auf `main` und jedem PR; ein Tag `vX.Y.Z` erzeugt ein Release mit der exe
- Repo-Dateien: `LICENSE` (MIT), `CHANGELOG.md` (bei jeder Änderung unter „Unveröffentlicht" ergänzen), `CONTRIBUTING.md`, `SECURITY.md`, Issue-/PR-Vorlagen in `.github/`

## Test-Stand (0.1.0)
Getestet unter Wine (Software-Rendering): Start, alle Shader, Bild, Video inkl. Schleife, Shader-Fehleranzeige, Einzelinstanz/Datei übergeben.
Auf echtem Windows 11 26H2 (Build 26300) getestet: Einbettung hinter die Symbole (Modus 24H2) auf 3 Bildschirmen
3840×2160 bei 150 %, Aurora animiert, Verdeckungs-Pause pro Bildschirm, sauberes Beenden per `taskkill`.
Vollbild-Pause (GPU-Last → 0 %), Katalog-Installation aus lokaler Datei (Selbsttest).
GPU-Last (RTX 4070 Ti SUPER, 4K, 30 fps, Taskmanager-3D): Aurora 12 % → 2,1 % durch `shader_res=1080` und
Aurora-Optimierung (überspringt verdeckte Bereiche; Bild pixelgenau gleich, mit GPU-Zeitstempeln gemessen 1,42 → 0,78 ms/Bild in 4K).
**Noch nicht auf echtem Windows getestet:** Audio-Reaktion sichtbar, Maus/Klick, Video, Katalog-Download über https.
Diagnose-Schalter: `desktop_layered=1`, `legacy_swapchain=1`.
Hinweis: GDI-Screenshots (CopyFromScreen) zeigen den Desktop-Bereich hier schwarz – Sichtprüfung braucht einen Menschen.

## Grundsätze
- Nichts im Hintergrund laufen lassen, was gerade keiner sieht (pausieren, Speicher freigeben).
- Keine neuen Abhängigkeiten ohne guten Grund; alles mit Windows-Bordmitteln.
- Hintergründe enthalten nie ausführbaren Code – nur Medien und HLSL.
