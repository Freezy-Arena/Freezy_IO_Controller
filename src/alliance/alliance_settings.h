#pragma once
#include <Preferences.h>
#include "alliance_policy.h"

namespace alliance {
inline uint32_t loadPressDebounceMs() {
    Preferences prefs;
    if (!prefs.begin("alliance", true)) return DefaultPressDebounceMs;
    uint32_t value = prefs.getUInt("pressDebounceMs", DefaultPressDebounceMs);
    prefs.end();
    return value <= MaxPressDebounceMs ? value : DefaultPressDebounceMs;
}

inline bool savePressDebounceMs(uint32_t value) {
    if (value > MaxPressDebounceMs) return false;
    Preferences prefs;
    if (!prefs.begin("alliance", false)) return false;
    bool saved = prefs.putUInt("pressDebounceMs", value) == sizeof(uint32_t);
    prefs.end();
    return saved;
}
}
