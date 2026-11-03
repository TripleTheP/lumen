# Sicherheit

## Sicherheitslücken melden

Bitte **nicht** öffentlich als Issue melden. Nutze stattdessen auf GitHub
**Security → Report a vulnerability** (vertrauliche Meldung an die Maintainer).

## Was Lumen tut und was nicht

- Hintergründe enthalten nie ausführbaren Code: Shader laufen nur auf der Grafikkarte, sonst gibt es nur Bilder und Videos.
- Der Online-Katalog lädt ausschließlich über `https://`. Zip-Pakete werden mit dem in Windows eingebauten `tar.exe` entpackt.
- Lumen braucht keine Adminrechte und sendet keine Telemetrie.
- Die exe ist nicht signiert, deshalb warnt Windows-SmartScreen beim ersten Start.
