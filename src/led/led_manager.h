#pragma once
#include <FastLED.h>
#include <Preferences.h>

#define LED_PIN     38
#define LED_DEFAULT_COUNT 125
#define LED_MAX_LEDS      750
#define LED_TYPE    WS2812B
#define LED_PREFS_NS "leds"

enum LedColorOrder : uint8_t {
    LED_ORDER_RGB = 0,
    LED_ORDER_RBG,
    LED_ORDER_GRB,
    LED_ORDER_GBR,
    LED_ORDER_BRG,
    LED_ORDER_BGR
};

class LedManager {
public:
    void begin(bool stopController = false);
    void update();
    void setLedRaw(uint16_t index, CRGB color);  // Set without calling show()
    void show();  

    // Basic controls
    void setAll(CRGB color);
    void setLed(uint16_t index, CRGB color);
    void clear();
    void setBrightness(uint8_t brightness);

    // Configuration
    uint16_t getLedCount() const;
    uint16_t getMaxLedCount() const;
    void setLedCount(uint16_t count);
    LedColorOrder getColorOrder() const;
    const char* getColorOrderName() const;
    bool setColorOrderByName(const String& name);
    void loadPreferences();
    void savePreferences();
    static constexpr uint32_t MaxWinnerSeconds = 3600;
    uint32_t getWinnerDelaySeconds() const { return _winnerDelaySeconds; }
    uint32_t getWinnerDurationSeconds() const { return _winnerDurationSeconds; }
    void setWinnerTiming(uint32_t delaySeconds, uint32_t durationSeconds);
    void saveWinnerTimingPreferences();
    // Preserve normal render data while a temporary winner frame is displayed.
    void setWinnerOverride(bool active, CRGB color);

    // Test patterns
    void showRainbow(uint8_t deltaHue = 7);
    void showSolid(CRGB color);
    void showChase(CRGB color, uint16_t speed = 50);

private:
    CRGB _leds[LED_MAX_LEDS]; // Latest normal frame, maintained during overrides.
    CRGB _lastShown[LED_MAX_LEDS]; // Physical buffer registered with FastLED.
    uint8_t _lastShownBrightness = 0;
    bool _hasShown = false;
    Preferences _prefs;
    uint16_t _ledCount = LED_DEFAULT_COUNT;
    uint16_t _outputCount = 300;
    LedColorOrder _colorOrder = LED_ORDER_BRG;
    uint8_t _brightness = 128;
    uint32_t _lastUpdate = 0;
    uint16_t _chasePos = 0;
    uint8_t _rainbowHue = 0;
    bool _dirty = false;
    bool _winnerOverride = false;
    CRGB _winnerColor = CRGB::Black;
    uint32_t _winnerDelaySeconds = 0;
    uint32_t _winnerDurationSeconds = 10;
    void loadWinnerTimingPreferences();
};
