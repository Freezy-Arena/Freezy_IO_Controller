#pragma once
#include "alliance_policy.h"
#include "../network/network_manager.h"
#include "../led/led_manager.h"
#include "../websocket/ws_manager.h"
#include "../input_status.h"

class AllianceStops {
public:
    void begin(DeviceRole role);
    void serviceStops(WsManager& ws, bool networkConnected);
    void update(LedManager& leds, uint32_t wsMessages);
    InputStatusSnapshot inputStatus();
private:
    static void sampleTask(void* self);
    void sample();
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    alliance::StopBank<256> _bank;
    alliance::PressDebounce _debounce[alliance::InputCount];
    bool _rawPressed[alliance::InputCount]{};
    uint32_t _pressDebounceMs = alliance::DefaultPressDebounceMs;
    alliance::StopBank<256>::Candidate _flight;
    bool _blue = false, _inFlight = false, _connected = false;
    bool _sampleSeen = false, _heartbeatOn = false;
    uint8_t _failures = 0, _heartbeat = 0;
    uint32_t _sentAt = 0, _nextSend = 0, _lastHeartbeat = 0, _lastReport = 0;
    uint32_t _lastSampleUs = 0, _sampleGapUs = 0, _ackMaxMs = 0;
    uint32_t _sampledAt = 0;
    uint32_t _sent = 0, _acks = 0, _retries = 0, _roundTripMs = 0;
    uint32_t _lastLoop = 0, _loopGapMs = 0, _ledMaxMs = 0;
};
