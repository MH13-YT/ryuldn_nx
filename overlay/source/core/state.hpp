#pragma once

#include <switch.h>

/* ===== APPLICATION STATE ===== */
enum class State {
    Uninit,   // Service not initialized
    Error,    // Service connection failed
    Loaded,   // Ready to use
};

/* ===== GLOBAL STATE ===== */
extern Service g_ldnSrv;
extern State g_state;
extern char g_errorMessage[64];

/* ===== COLORS & STYLING ===== */
constexpr u32 COLOR_ACCENT = 0x00B0E0FF;    // Cyan
constexpr u32 COLOR_SUCCESS = 0xFF00FF00;   // Green
constexpr u32 COLOR_ERROR = 0xFF0000FF;     // Red
constexpr u32 COLOR_TEXT = 0xFFFFFFFF;      // White
