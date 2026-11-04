# Änderungen

Format nach [Keep a Changelog](https://keepachangelog.com/de/1.1.0/), Versionen nach [SemVer](https://semver.org/lang/de/).

## [Unveröffentlicht]

### Hinzugefügt
- Modernes Erscheinungsbild: Einstellungsfenster und Tray-Menü folgen dem hellen/dunklen Modus von Windows (auch beim Wechsel zur Laufzeit),
  dunkle Titelleiste, eigene Schalter, Knöpfe, Reiter und Regler im selben Stil, Akzentfarbe, Hover-Zustände.
- Einstellungsfenster mit Vorschaubildern: Raster aller Hintergründe, Regler und Farbfelder für Eigenschaften, alle Schalter des
  Menüs. Linksklick auf das Tray-Symbol öffnet es (Rechtsklick: Menü wie bisher). Skaliert mit der Bildschirm-DPI.
- Animierte GIFs (bisher wurde nur das erste Bild gezeigt): Teilbilder und alle Entsorgungsarten, Bilddauer aus der Datei,
  gezeichnet wird nur bei fälligem Bildwechsel.
- Zeitplan: Hintergrund nach Tageszeit (`zeitplan.txt`, Menü → Einstellungen → Zeitplan). Zeilen wie `06:00 | Beispiele\Aurora`,
  gilt über Mitternacht, wird live neu geladen, eigene Wahl bleibt bis zum nächsten Wechsel.
- Einstellbare Shader-Werte: `// @param tempo float 1.0 0.2 3.0 Text` im Shader, `param.tempo=2` in `wallpaper.ini`,
  Menü → „Eigenschaften … ändern". Änderungen an der ini werden sofort übernommen. Die Aurora bietet Tempo, Helligkeit und zwei Farben.
- Panorama: ein Hintergrund über mehrere Bildschirme (Menü → Einstellungen, `span=1`). Shader sehen den ganzen Desktop;
  Klicks, Maus und Zeit laufen über die Bildschirmränder hinweg zusammen.
- Pause bei Vollbild-Programmen (Spiel, Video): alle Bildschirme pausieren (`pause_fullscreen`, auch im Menü).
- `shader_res`: reine Shader werden höchstens in 1080p berechnet und hochskaliert (Menü → Einstellungen → Renderauflösung).
- `make DEBUG=1` (Debug-Build mit Symbolen) und `make test` (Selbsttest, `tests/selftest.cpp`).
- VS-Code-Einrichtung (`.vscode/`): Bauen, Debuggen mit gdb, IntelliSense für MSYS2.

### Geändert
- Aurora zeichnet verdeckte Bereiche nicht mehr (Bild unverändert, etwa halbe GPU-Zeit). Unveränderte Kopien der
  alten Fassung in der Bibliothek werden beim Start ersetzt, selbst bearbeitete bleiben.
- `iTime` beginnt jede Stunde wieder bei 0, damit Animationen auch nach Tagen flüssig bleiben.
- Auf einem 4K-Bildschirm sinkt die GPU-Last der Aurora von etwa 12 % auf 2 % (RTX 4070 Ti SUPER, 30 Bilder/s).

### Behoben
- `wallpaper.ini`: Kommentare hinter einem Wert (`source=video.mp4 ; Text`) wurden als Teil des Werts gelesen.
- Katalog-Pakete werden beim erneuten Installieren vollständig ersetzt, statt über den alten Ordner entpackt zu werden.
  Ist der Ordner gerade in Benutzung, bricht die Installation mit einer verständlichen Meldung ab.
- Lumen beendet sich sauber auf `WM_CLOSE` (z. B. `taskkill /im Lumen.exe` ohne `/F`).

## [0.1.0]

Erste Version: Shader, Bilder und Videos hinter den Desktop-Symbolen (Direct3D 11), Audio-Reaktion, Bibliothek,
Online-Katalog, Import von Wallpaper-Engine-Videos, automatische Pausen.

[Unveröffentlicht]: https://github.com/TripleTheP/lumen/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/TripleTheP/lumen/releases/tag/v0.1.0
