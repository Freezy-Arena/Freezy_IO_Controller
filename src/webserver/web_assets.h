#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <initializer_list>

namespace WebAssets {
struct Value {
    const char* name;
    String text;
};

// Values are HTML-escaped, for text and quoted attribute contexts only.
// {{titleBar}} and {{version}} are supplied centrally.
String render(const char* page, std::initializer_list<Value> values = {});
void registerStaticRoutes(AsyncWebServer& server);
}
