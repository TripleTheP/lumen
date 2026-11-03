#pragma once
#include <vector>

struct ExampleWallpaper {
    const wchar_t* folder;   // Ordnername unter Bibliothek\Beispiele
    const char* ini;         // Inhalt von wallpaper.ini
    const char* shader;      // Inhalt von shader.hlsl
};

const std::vector<ExampleWallpaper>& Examples();
const char* ShaderTemplate();   // Vorlage für "Neuen Shader erstellen"
const char* LibraryReadme();    // LIESMICH.txt in der Bibliothek
