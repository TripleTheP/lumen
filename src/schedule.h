#pragma once
#include "common.h"

// Zeitplan: Hintergrund nach Tageszeit. Datei %APPDATA%\Lumen\zeitplan.txt, eine Zeile pro Wechsel:
//   06:00 | Beispiele\Aurora
struct ScheduleEntry {
    int minute = 0;        // Minuten seit Mitternacht, 0..1439
    std::wstring id;       // Hintergrund-ID aus der Bibliothek oder "none"
};

// Liest die Datei; # beginnt eine Kommentarzeile, ungültige Zeilen werden übersprungen. Ergebnis nach Zeit sortiert,
// bei gleicher Uhrzeit gilt die spätere Zeile.
std::vector<ScheduleEntry> ParseSchedule(const std::string& text);

// Eintrag, der zur gegebenen Minute gilt: der letzte mit Uhrzeit <= minute, davor (nach Mitternacht) der letzte vom Vortag.
// nullptr, wenn die Liste leer ist.
const ScheduleEntry* ActiveScheduleEntry(const std::vector<ScheduleEntry>& entries, int minute);
