#pragma once
#include <stdint.h>

// ─── PLC Coil Index Map ───────────────────────────────────────────────────────
// Update this file each season to match the arena's coil assignments.
// Usage:  coils[COIL_STACK_LIGHT_GREEN]

enum PlcCoil : uint8_t {
    COIL_HEARTBEAT          = 0,
    COIL_MATCH_RESET        = 1,
    COIL_STACK_LIGHT_GREEN  = 2,
    COIL_STACK_LIGHT_ORANGE = 3,
    COIL_STACK_LIGHT_RED    = 4,
    COIL_STACK_LIGHT_BLUE   = 5,
    COIL_STACK_LIGHT_BUZZER = 6,
    COIL_FIELD_RESET_LIGHT  = 7,
    COIL_AWARDS_MODE_LIGHT  = 8,
    COIL_RED_HUB_MOTOR      = 9,
    COIL_BLUE_HUB_MOTOR     = 10,
    COIL_RED_HUB_LIGHT      = 11,
    COIL_BLUE_HUB_LIGHT     = 12,
    COIL_RED_WINS           = 13,
    COIL_BLUE_WINS          = 14,

    COIL_COUNT              // Always last — gives total number of coils
};