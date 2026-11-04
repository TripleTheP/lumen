#pragma once
#include "common.h"

// Befehle des Tray-Menüs. Das Einstellungsfenster löst dieselben Befehle aus, damit beide dasselbe tun.
enum : UINT {
    ID_PAUSE = 10, ID_ADDFILE, ID_NEWSHADER, ID_OPENLIB, ID_EXIT, ID_AUTOSTART, ID_BATTERY, ID_AUDIO, ID_WE,
    ID_OPENCONFIG, ID_CAT_REFRESH, ID_RELOAD, ID_ABOUT, ID_FULLSCREEN, ID_SHADER_RES, ID_SPAN, ID_PROPS, ID_SCHEDULE,
    ID_EDITSCHEDULE, ID_SETTINGS,
    ID_FPS_BASE = 100,        // +fps
    ID_SCALE_BASE = 400,      // +Prozent
    ID_CAT_BASE = 1000,       // +Index
    ID_ALL_BASE = 3000,       // +Index (999 = kein Live-Hintergrund)
    ID_SCREEN_BASE = 10000,   // +Bildschirm*1000 + Index
};
