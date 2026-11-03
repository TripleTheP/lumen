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
- `src/examples.cpp` – mitgelieferte Shader (Aurora, Punktfeld, Spektrum, Tag und Nacht) und Vorlage
- `catalog/` – Vorlage für einen selbst gehosteten Online-Katalog
- Einstellungen: `%APPDATA%\Lumen\config.ini`, Log: `%APPDATA%\Lumen\lumen.log`

## Bauen
- MinGW (Linux/WSL): `make` → `build/Lumen.exe`; MSYS2: `make CROSS=`
- Visual Studio: `cmake -B build -A x64` und `cmake --build build --config Release` (CMake/MSVC noch nicht getestet)
- GitHub Action `.github/workflows/build.yml` baut bei jedem Push

## Test-Stand (0.1.0)
Getestet unter Wine (Software-Rendering): Start, alle Shader, Bild, Video inkl. Schleife, Shader-Fehleranzeige, Einzelinstanz/Datei übergeben.
**Noch nicht auf echtem Windows getestet:** Einbettung hinter die Symbole (besonders 24H2), Audio-Erfassung, Maus/Klick,
Verdeckungs-Pause, Mehrschirm, Katalog-Download. Diagnose-Schalter: `desktop_layered=1`, `legacy_swapchain=1`.

## Grundsätze
- Nichts im Hintergrund laufen lassen, was gerade keiner sieht (pausieren, Speicher freigeben).
- Keine neuen Abhängigkeiten ohne guten Grund; alles mit Windows-Bordmitteln.
- Hintergründe enthalten nie ausführbaren Code – nur Medien und HLSL.
