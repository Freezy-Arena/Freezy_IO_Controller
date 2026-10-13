#include "web_assets.h"
#include "../firmware_version.h"
#include <cstring>

namespace {
struct EmbeddedAsset {
    const char* path;
    const char* mime;
    const uint8_t* data;
    size_t size;
    bool isStatic;
};
#include "web_assets_generated.h"

const EmbeddedAsset* findAsset(const String& path) {
    for (const auto& asset : WEB_ASSETS) {
        if (path == asset.path) return &asset;
    }
    return nullptr;
}

void appendEscaped(String& output, const String& value) {
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value[i]) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output += value[i]; break;
        }
    }
}
}

String WebAssets::render(const char* page, std::initializer_list<Value> values) {
    const auto* asset = findAsset(String("/template/") + page + ".html");
    if (!asset) {
        Serial.printf("[WEB] Missing template: %s\n", page);
        return "Template unavailable";
    }
    String output;
    output.reserve(asset->size + 256);
    const char* cursor = reinterpret_cast<const char*>(asset->data);
    // Single pass: inserted values are never interpreted as template markup.
    while (*cursor) {
        const char* start = strstr(cursor, "{{");
        if (!start) { output += cursor; break; }
        output.concat(cursor, start - cursor);
        const char* end = strstr(start + 2, "}}");
        if (!end) { output += start; break; }
        String key;
        key.concat(start + 2, end - start - 2);
        if (key == "titleBar" && strcmp(page, "titleBar") != 0) {
            output += render("titleBar");
        } else if (key == "version") {
            appendEscaped(output, FREEZY_FIRMWARE_VERSION);
        } else {
            bool found = false;
            for (const auto& value : values) {
                if (key == value.name) {
                    appendEscaped(output, value.text);
                    found = true;
                    break;
                }
            }
            if (!found) Serial.printf("[WEB] Missing template value: %s/%s\n", page, key.c_str());
        }
        cursor = end + 2;
    }
    return output;
}

void WebAssets::registerStaticRoutes(AsyncWebServer& server) {
    for (const auto& asset : WEB_ASSETS) {
        if (!asset.isStatic) continue; // Templates are only served through page handlers.
        const EmbeddedAsset* entry = &asset;
        server.on(entry->path, HTTP_GET, [entry](AsyncWebServerRequest* request) {
            auto* response = request->beginResponse(200, entry->mime, entry->data, entry->size);
            // Avoid retaining old CSS/JS across firmware updates, even development builds.
            response->addHeader("Cache-Control", "no-store");
            response->addHeader("X-Content-Type-Options", "nosniff");
            request->send(response);
        });
    }
}
