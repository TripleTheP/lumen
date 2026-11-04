#pragma once
#include <vector>
#include <cstdint>

struct ExampleWallpaper {
    const wchar_t* folder;   // Ordnername unter Bibliothek\Beispiele
    const char* ini;         // Inhalt von wallpaper.ini
    const char* shader;      // Inhalt von shader.hlsl
    std::vector<uint64_t> replaces;   // Fnv1a früherer Fassungen: Unveränderte Kopien davon werden beim Start ersetzt
};

const std::vector<ExampleWallpaper>& Examples();
const char* ShaderTemplate();   // Vorlage für "Neuen Shader erstellen"
const char* LibraryReadme();    // LIESMICH.txt in der Bibliothek
