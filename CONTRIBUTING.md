# Mitmachen

Danke für dein Interesse an Lumen! Fehlermeldungen, Ideen und Pull Requests sind willkommen.

## Fehler melden

Bitte über die [Issue-Vorlage](../../issues/new/choose) und mit:
- Windows-Version (Win+R → `winver`), Grafikkarte, Anzahl und Auflösung der Bildschirme
- Lumen-Version (Menü → *Über Lumen*)
- der Datei `%APPDATA%\Lumen\lumen.log`

## Grundsätze

Lumen soll **klein und sparsam** bleiben (siehe [CLAUDE.md](CLAUDE.md), Abschnitt „Grundsätze"):
- Nichts im Hintergrund laufen lassen, was gerade keiner sieht.
- Keine neuen Abhängigkeiten ohne guten Grund, alles mit Windows-Bordmitteln.
- Hintergründe enthalten nie ausführbaren Code, nur Medien und HLSL.

## Bauen und testen

Voraussetzung: [MSYS2](https://www.msys2.org/) mit `mingw-w64-ucrt-x86_64-gcc` und `make`
(für den Debugger zusätzlich `mingw-w64-ucrt-x86_64-gdb`). Im Terminal „MSYS2 UCRT64":

```bash
make CROSS= -j8        # build/Lumen.exe
make CROSS= DEBUG=1    # build/debug/Lumen.exe mit Symbolen
make CROSS= test       # Selbsttest (nur unter Windows)
```

In VS Code genügen Strg+Umschalt+B (Bauen) und F5 (Debuggen). Eine laufende Lumen-Instanz wird dabei vorher beendet.

## Pull Requests

- Eine Änderung pro Pull Request, mit kurzer Begründung.
- Der Build muss ohne Warnungen laufen, `make test` muss bestehen.
- Kommentare, UI-Texte und Commit-Nachrichten auf **Deutsch**.
- Ändert ein PR die Optik oder Leistung eines mitgelieferten Shaders, bitte Vorher/Nachher nennen.
- Änderst du einen Beispiel-Shader in `src/examples.cpp`, setze `replaces` auf `Fnv1a` der bisherigen Fassung,
  sonst bekommen bestehende Installationen die neue Fassung nicht.
- Trage die Änderung in [CHANGELOG.md](CHANGELOG.md) unter „Unveröffentlicht" ein.

Mit deinem Beitrag stimmst du zu, dass er unter der [MIT-Lizenz](LICENSE) des Projekts steht.
