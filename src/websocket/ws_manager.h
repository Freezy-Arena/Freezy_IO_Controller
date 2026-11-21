#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "../role_config.h"
#include "input_ack.h"

// Mirrors the Python register map
// Counter channel → PLC register
//   Ch0 → Reg 3    Ch2 → Reg 5
//   Ch1 → Reg 4    Ch3 → Reg 6
// Reg 1 = sum of Reg 3,4,5,6
// Reg 2 = sum of Reg 7,8,9,10  (reserved for future channels)

#define WS_RECONNECT_DELAY_MS   3000
// Websocket ping every 2 s; drop the connection after 2 pongs missing for
// 1.5 s each, so a dead connection is noticed in about 5 s, not minutes.
#define WS_HEARTBEAT_INTERVAL_MS 2000
#define WS_HEARTBEAT_TIMEOUT_MS  1500
#define WS_HEARTBEAT_MISSES      2
#define WS_MAX_MESSAGE_SIZE     (15 * 1024)
#define WS_PREFS_NS             "websocket"
extern bool _debugSerial;

#define WS_LOG(fmt, ...) if (_debugSerial) Serial.printf(fmt, ##__VA_ARGS__)
#define WS_PRINTLN(msg)  if (_debugSerial) Serial.println(msg)

// Coil callback — fired whenever coil state changes
// coils: pointer to bool array, count: number of coils
typedef void (*CoilCallback)(const bool* coils, uint8_t count);

// setLedMode callback for the server's RedMode and BlueMode fields.
typedef void (*LedModeCallback)(int redMode, int blueMode);


class WsManager {
public:
    void begin(const String& host, uint16_t port, const String& path = "/api/plc/websocket");
    void update();                          // Call from loop()

    void onSetLedMode(LedModeCallback cb);
    void setLedModeEnabled(bool enabled);
    
    bool isConnected();
    void configureStopRole(DeviceRole role) {
        _fmsTable = role == ROLE_FMS_TABLE;
        _stopInputs = isStopRole(role);
        _firstStopChannel = role == ROLE_BLUE_ALLIANCE ? INPUT_BLUE_1_ESTOP
                          : role == ROLE_RED_ALLIANCE ? INPUT_RED_1_ESTOP : INPUT_FIELD_ESTOP;
        _stopChannelCount = isAllianceRole(role) ? 6 : 1;
    }
    uint32_t messageCount() const { return _messages; }
    // millis() when anything last arrived from the arena, 0 if nothing yet.
    uint32_t lastReceiveMs() const { return _lastReceive; }
    // Main-loop only. One untagged ACK at a time, restricted to this role's inputs.
    // Stop writes bypass the optional hub telemetry flag.
    bool sendStopInput(bool state, uint8_t channel = INPUT_FIELD_ESTOP);
    InputReply takeStopReply() { return _inputAck.take(); }

    // Send input states 
    void sendInputs(const bool* states, uint8_t count);
    void sendInput(const bool state, uint8_t channel); // Single channel version for testing

    // Send all four counter values as registers in one batch
    void sendCounters(int64_t ch0, int64_t ch1, int64_t ch2, int64_t ch3,
                  const RoleConfig& role);

    // Register a callback for incoming coil updates
    void onCoilUpdate(CoilCallback cb);

    // Preferences — ready for webserver config later
    String  arenaHost = "10.0.100.5";
    uint16_t arenaPort = 8080;
    String authUsername = "admin";
    String authPassword = "password";
    bool sendRegistersEnabled = true;
    bool sendInputsEnabled = true;
    void resetSessionAuth();
    void loadPreferences();
    void savePreferences();

private:
    WebSocketsClient    _ws;
    bool                _connected  = false;
    bool                _usingSessionAuth = false;
    String              _sessionCookie;
    uint32_t            _lastRetry  = 0;
    bool                _receivingTextFragment = false;
    bool                _ledModeEnabled = true;
    String              _fragmentBuffer;
    CoilCallback        _coilCb     = nullptr;
    Preferences         _prefs;
    bool _fmsTable = false;
    bool _stopInputs = false;
    uint8_t _firstStopChannel = INPUT_FIELD_ESTOP, _stopChannelCount = 1;
    InputAck _inputAck;
    uint32_t _messages = 0, _arenaMessages = 0, _plcMessages = 0, _parseErrors = 0;
    uint32_t _lastReceive = 0;
    uint32_t _disconnects = 0, _wsGap = 0, _wsWork = 0, _lastUpdate = 0, _lastReport = 0;

    void _onEvent(WStype_t type, uint8_t* payload, size_t length);
    bool _appendTextFragment(const uint8_t* payload, size_t length);
    void _handleMessage(const String& raw);
    void _handleCoilChange(JsonObject data);
    bool _authenticateWithSession();
    void _sendJson(const String& type, JsonDocument& data);
    LedModeCallback _ledModeCb = nullptr;
    void _handleSetLedMode(JsonObject data);
};
