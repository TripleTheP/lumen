#include "schedule.h"
#include <algorithm>
#include <cwctype>
#include <sstream>

// "6:30" oder "06:30" -> Minuten; false bei allem anderen
static bool ParseTime(const std::wstring& s, int& minute) {
    size_t colon = s.find(L':');
    if (colon == std::wstring::npos || colon == 0 || colon > 2 || s.size() - colon - 1 != 2) return false;
    for (size_t i = 0; i < s.size(); i++)
        if (i != colon && !iswdigit(s[i])) return false;
    int h = _wtoi(s.substr(0, colon).c_str()), m = _wtoi(s.substr(colon + 1).c_str());
    if (h > 23 || m > 59) return false;
    minute = h * 60 + m;
    return true;
}

std::vector<ScheduleEntry> ParseSchedule(const std::string& text) {
    std::map<int, std::wstring> byTime;   // gleiche Uhrzeit: die spätere Zeile gewinnt
    std::wstringstream ss(Utf8ToWide(text));
    std::wstring line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#') continue;
        size_t bar = line.find(L'|');
        if (bar == std::wstring::npos) continue;
        int minute;
        std::wstring id = Trim(line.substr(bar + 1));
        if (!ParseTime(Trim(line.substr(0, bar)), minute) || id.empty()) continue;
        byTime[minute] = id;
    }
    std::vector<ScheduleEntry> out;
    for (auto& kv : byTime) out.push_back({kv.first, kv.second});
    return out;
}

const ScheduleEntry* ActiveScheduleEntry(const std::vector<ScheduleEntry>& entries, int minute) {
    if (entries.empty()) return nullptr;
    const ScheduleEntry* active = &entries.back();   // vor dem ersten Eintrag gilt noch der letzte von gestern
    for (auto& e : entries)
        if (e.minute <= minute) active = &e;
    return active;
}
