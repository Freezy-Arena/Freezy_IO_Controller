#pragma once
#include "../firmware_version.h"

// Shared markup uses scoped inline styles to fit every page's existing layout.
#define DEVICE_TITLE_BAR \
    "<header style=\"display:flex;align-items:center;justify-content:space-between;" \
    "flex-wrap:wrap;gap:12px;padding:0 0 18px;margin:0 0 24px;" \
    "border-bottom:1px solid #373d64;font-family:system-ui,sans-serif;\">" \
    "<a href=\"/\" style=\"color:#f1f5f9;text-decoration:none;font-size:1.1rem;" \
    "font-weight:700;\">Freexy IO Contoller</a>" \
    "<span aria-label=\"Firmware version " FREEZY_FIRMWARE_VERSION "\" " \
    "style=\"display:inline-block;padding:5px 10px;border:1px solid #373d64;" \
    "border-radius:999px;background:#20243a;color:#c7d2fe;font-size:.8rem;" \
    "white-space:nowrap;\">v" FREEZY_FIRMWARE_VERSION "</span></header>"
