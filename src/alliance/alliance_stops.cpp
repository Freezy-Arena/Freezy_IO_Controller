#include "alliance_stops.h"
#include "alliance_settings.h"

void AllianceStops::begin(DeviceRole role) {
    _blue = role == ROLE_BLUE_ALLIANCE;
    _pressDebounceMs = alliance::loadPressDebounceMs();
    for (uint8_t pin : alliance::Pins) pinMode(pin, INPUT);
    sample(); // Retain the boot snapshot even when Ethernet is unavailable.
    if (xTaskCreatePinnedToCore(sampleTask, "alliance-sample", 3072, this, 3, nullptr, 1) != pdPASS) {
        portENTER_CRITICAL(&_mux);
        _bank.fault = true;
        portEXIT_CRITICAL(&_mux);
        Serial.println("[ALLIANCE] FAULT: sampling task allocation failed; asserting all six stops");
    }
}

void AllianceStops::sampleTask(void* self) {
    auto& stops = *static_cast<AllianceStops*>(self);
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        stops.sample();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);
    }
}

void AllianceStops::sample() {
    uint32_t nowUs = micros(), now = millis();
    bool states[alliance::InputCount];
    for (uint8_t i = 0; i < alliance::InputCount; ++i) {
        states[i] = fms::wireState(digitalRead(alliance::Pins[i]) == HIGH);
    }
    portENTER_CRITICAL(&_mux);
    if (_sampleSeen) _sampleGapUs = max(_sampleGapUs, uint32_t(nowUs - _lastSampleUs));
    _sampleSeen = true;
    _lastSampleUs = nowUs;
    _sampledAt = now;
    for (uint8_t i = 0; i < alliance::InputCount; ++i) {
        _rawPressed[i] = !states[i];
        _bank.observe(i, _debounce[i].sample(states[i], now, _pressDebounceMs), now);
    }
    portEXIT_CRITICAL(&_mux);
}

InputStatusSnapshot AllianceStops::inputStatus() {
    InputStatusSnapshot snapshot;
    portENTER_CRITICAL(&_mux);
    snapshot.sampled = _sampleSeen;
    snapshot.sampledAt = _sampledAt;
    snapshot.fault = _bank.fault;
    snapshot.count = alliance::InputCount;
    for (uint8_t i = 0; i < alliance::InputCount; ++i) snapshot.pressed[i] = _rawPressed[i];
    portEXIT_CRITICAL(&_mux);
    return snapshot;
}

void AllianceStops::serviceStops(WsManager& ws, bool networkConnected) {
    uint32_t now = millis();
    _connected = networkConnected && ws.isConnected();
    InputReply reply = ws.takeStopReply();
    if (_inFlight && (reply == InputReply::Success || reply == InputReply::Failed)) {
        _roundTripMs = max(_roundTripMs, uint32_t(now - _sentAt));
        if (reply == InputReply::Success) {
            portENTER_CRITICAL(&_mux);
            bool popped = _bank.acknowledge(_flight, now);
            portEXIT_CRITICAL(&_mux);
            if (popped) _ackMaxMs = max(_ackMaxMs, uint32_t(now - _flight.event.sampledAt));
            ++_acks;
            _failures = 0;
            _nextSend = now;
            _heartbeat = 1;
        } else {
            ++_retries;
            if (_failures < 5) ++_failures;
            _nextSend = now + fms::retryDelay(_failures);
            _heartbeat = 2;
        }
        _inFlight = false;
    }
    if (!_connected || _inFlight || int32_t(now - _nextSend) < 0) return;
    alliance::StopBank<256>::Candidate candidate;
    portENTER_CRITICAL(&_mux);
    bool due = _bank.next(now, candidate);
    portEXIT_CRITICAL(&_mux);
    // The main loop alone owns WebSocketsClient; no network I/O under the lock.
    if (due && ws.sendStopInput(candidate.state, alliance::channel(_blue, candidate.index))) {
        _flight = candidate;
        _inFlight = true;
        _sentAt = now;
        ++_sent;
    }
}

void AllianceStops::update(LedManager& leds, uint32_t wsMessages) {
    uint32_t now = millis();
    if (_lastLoop) _loopGapMs = max(_loopGapMs, uint32_t(now - _lastLoop));
    _lastLoop = now;
    portENTER_CRITICAL(&_mux);
    bool fault = _bank.fault;
    uint32_t sampleGapUs = _sampleGapUs;
    size_t depth[alliance::InputCount], high[alliance::InputCount];
    uint32_t lost = 0;
    for (uint8_t i = 0; i < alliance::InputCount; ++i) {
        depth[i] = _bank.inputs[i].count;
        high[i] = _bank.inputs[i].highWater;
        lost += _bank.inputs[i].unretained;
    }
    portEXIT_CRITICAL(&_mux);
    if (uint32_t(now - _lastHeartbeat) >= 500) {
        _lastHeartbeat = now;
        _heartbeatOn = !_heartbeatOn;
        CRGB color = !_connected ? CRGB::Red : _heartbeat == 1 ? CRGB(50, 50, 50)
                   : _heartbeat == 2 ? CRGB::Orange : CRGB::Black;
        leds.setLedRaw(0, _heartbeatOn ? color : CRGB::Black);
        _heartbeat = 0;
    }
    // Alternate fault indication is distinguishable from the normal red role.
    leds.setLedRaw(1, fault ? (_heartbeatOn ? CRGB::Red : CRGB::Black)
                           : (_blue ? CRGB::Blue : CRGB::Red));
    leds.setLedRaw(2, wsMessages % 2 ? CRGB(0, 50, 0) : CRGB::Black);
    uint32_t ledStart = millis();
    leds.show();
    _ledMaxMs = max(_ledMaxMs, uint32_t(millis() - ledStart));
    if (uint32_t(now - _lastReport) >= 5000) {
        _lastReport = now;
        Serial.printf("[ALLIANCE] %s WS=%s FAULT=%d unretained=%lu sent=%lu ack=%lu retry=%lu sample_gap_us=%lu loop_gap_ms=%lu led_max_ms=%lu ack_max_ms=%lu rtt_ms=%lu\n",
            _blue ? "BLUE_ALLIANCE" : "RED_ALLIANCE", _connected ? "UP" : "DOWN", fault, lost,
            _sent, _acks, _retries, sampleGapUs, _loopGapMs, _ledMaxMs, _ackMaxMs, _roundTripMs);
        Serial.printf("[ALLIANCE QUEUES] depth=%u,%u,%u,%u,%u,%u high=%u,%u,%u,%u,%u,%u\n",
            unsigned(depth[0]), unsigned(depth[1]), unsigned(depth[2]), unsigned(depth[3]), unsigned(depth[4]), unsigned(depth[5]),
            unsigned(high[0]), unsigned(high[1]), unsigned(high[2]), unsigned(high[3]), unsigned(high[4]), unsigned(high[5]));
    }
}
