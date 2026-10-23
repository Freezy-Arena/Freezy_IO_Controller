#include "led_manager.h"

void LedManager::begin(bool stopController) {
    _outputCount = stopController ? 750 : 300;
    CLEDController* controller = nullptr;
    if (stopController) {
        // Stop-controller LEDs stay fixed, but winner strobes use the same
        // saved delay and duration as hub winner lights.
        loadWinnerTimingPreferences();
        _ledCount = 750;
        _colorOrder = LED_ORDER_GRB;
        _brightness = 15;
        controller = &FastLED.addLeds<LED_TYPE, 47, GRB>(_lastShown, _outputCount);
        FastLED.setMaxPowerInMilliWatts(900);
    } else {
        loadPreferences();
        switch (_colorOrder) {
            case LED_ORDER_RGB: controller = &FastLED.addLeds<LED_TYPE, LED_PIN, RGB>(_lastShown, _outputCount); break;
            case LED_ORDER_RBG: controller = &FastLED.addLeds<LED_TYPE, LED_PIN, RBG>(_lastShown, _outputCount); break;
            case LED_ORDER_GRB: controller = &FastLED.addLeds<LED_TYPE, LED_PIN, GRB>(_lastShown, _outputCount); break;
            case LED_ORDER_GBR: controller = &FastLED.addLeds<LED_TYPE, LED_PIN, GBR>(_lastShown, _outputCount); break;
            case LED_ORDER_BRG: controller = &FastLED.addLeds<LED_TYPE, LED_PIN, BRG>(_lastShown, _outputCount); break;
            case LED_ORDER_BGR: controller = &FastLED.addLeds<LED_TYPE, LED_PIN, BGR>(_lastShown, _outputCount); break;
            default: controller = &FastLED.addLeds<LED_TYPE, LED_PIN, BRG>(_lastShown, _outputCount); break;
        }
        controller->setCorrection(TypicalLEDStrip);
    }
    FastLED.setBrightness(_brightness);
    _dirty = true;  // Push the initial off frame even when the buffer starts zeroed.
    clear();
    Serial.printf("[LED] Initialized with %u LEDs, color order %s\n",
                  _ledCount, getColorOrderName());
}

void LedManager::update() {
    // Hook for animations that need periodic updates
    // Called from main loop
}

void LedManager::setLedRaw(uint16_t index, CRGB color) {
    if (index < _ledCount && _leds[index] != color) {
        _leds[index] = color;
        _dirty = true;
    }
}

void LedManager::show() {
    if (!_dirty) return;
    // Compare the completed frame: animations may change pixels and then
    // restore them while rendering, leaving the final output unchanged.
    bool changed = !_hasShown || _brightness != _lastShownBrightness;
    for (uint16_t i = 0; i < _outputCount; i++) {
        const CRGB next = _winnerOverride
            ? (i < _ledCount ? _winnerColor : CRGB::Black) : _leds[i];
        if (next != _lastShown[i]) {
            changed = true;
            _lastShown[i] = next;
        }
    }
    if (!changed) {
        _dirty = false;
        return;
    }
    FastLED.show();
    _lastShownBrightness = _brightness;
    _hasShown = true;
    _dirty = false;
}

void LedManager::setAll(CRGB color) {
    for (uint16_t i = 0; i < _outputCount; i++) {
        CRGB next = i < _ledCount ? color : CRGB::Black;
        if (_leds[i] != next) {
            _leds[i] = next;
            _dirty = true;
        }
    }
    show();
}

void LedManager::setLed(uint16_t index, CRGB color) {
    setLedRaw(index, color);
    show();
}

void LedManager::clear() {
    for (uint16_t i = 0; i < _outputCount; i++) {
        if (_leds[i] != CRGB::Black) {
            _leds[i] = CRGB::Black;
            _dirty = true;
        }
    }
    show();
}

void LedManager::setBrightness(uint8_t brightness) {
    if (_brightness == brightness) return;
    _brightness = brightness;
    FastLED.setBrightness(_brightness);
    _dirty = true;
    show();
}

void LedManager::showRainbow(uint8_t deltaHue) {
    CRGB frame[LED_MAX_LEDS];
    fill_rainbow(frame, _ledCount, _rainbowHue, deltaHue);
    if (_ledCount < _outputCount) {
        fill_solid(frame + _ledCount, _outputCount - _ledCount, CRGB::Black);
    }
    for (uint16_t i = 0; i < _outputCount; i++) {
        if (_leds[i] != frame[i]) {
            _leds[i] = frame[i];
            _dirty = true;
        }
    }
    _rainbowHue++;
    show();
}

void LedManager::showSolid(CRGB color) {
    setAll(color);
}

void LedManager::showChase(CRGB color, uint16_t speed) {
    if (millis() - _lastUpdate < speed) return;
    _lastUpdate = millis();

    if (_ledCount == 0) return;

    for (uint16_t i = 0; i < _ledCount; i++) {
        setLedRaw(i, CRGB::Black);
    }
    setLedRaw(_chasePos % _ledCount, color);
    show();
    _chasePos++;
}

uint16_t LedManager::getLedCount() const {
    return _ledCount;
}

uint16_t LedManager::getMaxLedCount() const {
    return _outputCount;
}

void LedManager::setLedCount(uint16_t count) {
    _ledCount = constrain(count, (uint16_t)1, (uint16_t)_outputCount);
}

LedColorOrder LedManager::getColorOrder() const {
    return _colorOrder;
}

const char* LedManager::getColorOrderName() const {
    switch (_colorOrder) {
        case LED_ORDER_RGB: return "RGB";
        case LED_ORDER_RBG: return "RBG";
        case LED_ORDER_GRB: return "GRB";
        case LED_ORDER_GBR: return "GBR";
        case LED_ORDER_BRG: return "BRG";
        case LED_ORDER_BGR: return "BGR";
        default: return "BRG";
    }
}

bool LedManager::setColorOrderByName(const String& name) {
    if (name == "RGB") _colorOrder = LED_ORDER_RGB;
    else if (name == "RBG") _colorOrder = LED_ORDER_RBG;
    else if (name == "GRB") _colorOrder = LED_ORDER_GRB;
    else if (name == "GBR") _colorOrder = LED_ORDER_GBR;
    else if (name == "BRG") _colorOrder = LED_ORDER_BRG;
    else if (name == "BGR") _colorOrder = LED_ORDER_BGR;
    else return false;
    return true;
}

void LedManager::loadPreferences() {
    _prefs.begin(LED_PREFS_NS, true);
    setLedCount(_prefs.getUShort("count", LED_DEFAULT_COUNT));
    uint8_t savedOrder = _prefs.getUChar("colorOrder", LED_ORDER_BRG);
    _colorOrder = savedOrder <= LED_ORDER_BGR
        ? static_cast<LedColorOrder>(savedOrder)
        : LED_ORDER_BRG;
    _winnerDelaySeconds = _prefs.getUInt("winDelaySec", 0);
    _winnerDurationSeconds = _prefs.getUInt("winDurationSec", 10);
    if (_winnerDelaySeconds > MaxWinnerSeconds) _winnerDelaySeconds = 0;
    if (_winnerDurationSeconds > MaxWinnerSeconds) _winnerDurationSeconds = 10;
    _prefs.end();
}

void LedManager::loadWinnerTimingPreferences() {
    _prefs.begin(LED_PREFS_NS, true);
    _winnerDelaySeconds = _prefs.getUInt("winDelaySec", 0);
    _winnerDurationSeconds = _prefs.getUInt("winDurationSec", 10);
    if (_winnerDelaySeconds > MaxWinnerSeconds) _winnerDelaySeconds = 0;
    if (_winnerDurationSeconds > MaxWinnerSeconds) _winnerDurationSeconds = 10;
    _prefs.end();
}

void LedManager::saveWinnerTimingPreferences() {
    _prefs.begin(LED_PREFS_NS, false);
    _prefs.putUInt("winDelaySec", _winnerDelaySeconds);
    _prefs.putUInt("winDurationSec", _winnerDurationSeconds);
    _prefs.end();
}

void LedManager::savePreferences() {
    _prefs.begin(LED_PREFS_NS, false);
    _prefs.putUShort("count", _ledCount);
    _prefs.putUChar("colorOrder", static_cast<uint8_t>(_colorOrder));
    _prefs.putUInt("winDelaySec", _winnerDelaySeconds);
    _prefs.putUInt("winDurationSec", _winnerDurationSeconds);
    _prefs.end();
    Serial.printf("[LED] Prefs saved - count: %u, color order: %s\n",
                  _ledCount, getColorOrderName());
}

void LedManager::setWinnerTiming(uint32_t delaySeconds, uint32_t durationSeconds) {
    if (delaySeconds > MaxWinnerSeconds || durationSeconds > MaxWinnerSeconds) return;
    _winnerDelaySeconds = delaySeconds;
    _winnerDurationSeconds = durationSeconds;
}

void LedManager::setWinnerOverride(bool active, CRGB color) {
    if (_winnerOverride == active && (!active || _winnerColor == color)) return;
    _winnerOverride = active;
    _winnerColor = color;
    _dirty = true;
    show();
}
