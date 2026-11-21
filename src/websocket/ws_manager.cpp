#include "ws_manager.h"
#include "input_map.h"
#include <Preferences.h>
#include <HTTPClient.h>

static constexpr const char* WS_SESSION_COOKIE = "session_token";

static String urlEncode(const String& value) {
    static const char hex[] = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);

    for (size_t i = 0; i < value.length(); i++) {
        uint8_t c = static_cast<uint8_t>(value[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

// ─── Preferences ─────────────────────────────────────────────────────────────

void WsManager::loadPreferences() {
    _prefs.begin(WS_PREFS_NS, true);
    arenaHost = _prefs.getString("arenaHost", "10.0.100.5");
    arenaPort = _prefs.getUShort("arenaPort", 8080);
    authUsername = _prefs.getString("authUsername", "admin");
    authPassword = _prefs.getString("authPassword", "password");
    sendRegistersEnabled = _prefs.getBool("setRegisters", true);
    sendInputsEnabled = _prefs.getBool("setInput", true);
    _prefs.end();
    //Serial.printf("[WS] Prefs loaded — %s:%d\n", arenaHost.c_str(), arenaPort);
    WS_LOG("[WS] Prefs loaded — %s:%d\n", arenaHost.c_str(), arenaPort);
}

void WsManager::savePreferences() {
    _prefs.begin(WS_PREFS_NS, false);
    _prefs.putString("arenaHost", arenaHost);
    _prefs.putUShort("arenaPort", arenaPort);
    _prefs.putString("authUsername", authUsername);
    _prefs.putString("authPassword", authPassword);
    _prefs.putBool("setRegisters", sendRegistersEnabled);
    _prefs.putBool("setInput", sendInputsEnabled);
    _prefs.end();
    Serial.println("[WS] Prefs saved");
}

// ─── Begin ────────────────────────────────────────────────────────────────────

void WsManager::begin(const String& host, uint16_t port, const String& path) {
    loadPreferences();

    // Allow override from caller (used before prefs are populated)
    if (host.length()) arenaHost = host;
    if (port)          arenaPort = port;


    _ws.begin(arenaHost.c_str(), arenaPort, path.c_str());
    String origin = "Origin: http://" + arenaHost + ":" + String(arenaPort);
    _ws.setExtraHeaders(origin.c_str());
    _ws.onEvent([this](WStype_t t, uint8_t* p, size_t l) {
        _onEvent(t, p, l);
    });
    _ws.setReconnectInterval(WS_RECONNECT_DELAY_MS);
    _ws.enableHeartbeat(WS_HEARTBEAT_INTERVAL_MS, WS_HEARTBEAT_TIMEOUT_MS, WS_HEARTBEAT_MISSES);

    Serial.printf("[WS] Connecting to ws://%s:%d%s\n", arenaHost.c_str(), arenaPort, path.c_str());
}

// ─── Update (call from loop) ──────────────────────────────────────────────────

void WsManager::update() {
    uint32_t now = millis();
    if (_lastUpdate) _wsGap = max(_wsGap, uint32_t(now - _lastUpdate));
    _lastUpdate = now;
    _ws.loop();
    if (_stopInputs) {
        _inputAck.expire(millis());
        if (_inputAck.resetRequired) {
            _ws.disconnect();
            _connected = false;
            _inputAck.disconnected();
        }
    }
    _wsWork = max(_wsWork, uint32_t(millis() - now));
    if (_debugSerial && _stopInputs && uint32_t(now - _lastReport) >= 5000) {
        _lastReport = now;
        Serial.printf("[WS TIMING] gap_ms=%lu work_ms=%lu messages=%lu arena=%lu plc=%lu parse_errors=%lu disconnects=%lu\n",
            _wsGap, _wsWork, _messages, _arenaMessages, _plcMessages, _parseErrors, _disconnects);
    }
}

// ─── Connection state ─────────────────────────────────────────────────────────

bool WsManager::isConnected() {
    return _connected;
}

void WsManager::resetSessionAuth() {
    _usingSessionAuth = false;
    _sessionCookie = "";
    String origin = "Origin: http://" + arenaHost + ":" + String(arenaPort);
    _ws.setExtraHeaders(origin.c_str());
}

bool WsManager::_authenticateWithSession() {
    HTTPClient http;
    String loginUrl = "http://" + arenaHost + ":" + String(arenaPort) + "/login";
    const char* responseHeaders[] = {"Set-Cookie"};

    if (!http.begin(loginUrl)) {
        Serial.println("[WS] Session login failed: could not start HTTP request");
        return false;
    }

    http.collectHeaders(responseHeaders, 1);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String form = "username=" + urlEncode(authUsername)
                + "&password=" + urlEncode(authPassword);
    int status = http.POST(form);
    String setCookie = http.header("Set-Cookie");
    http.end();

    String cookiePrefix = String(WS_SESSION_COOKIE) + "=";
    int tokenStart = setCookie.indexOf(cookiePrefix);
    if (status != HTTP_CODE_SEE_OTHER || tokenStart < 0) {
        Serial.printf("[WS] Session login failed: HTTP %d, session cookie missing\n", status);
        return false;
    }

    tokenStart += cookiePrefix.length();
    int tokenEnd = setCookie.indexOf(';', tokenStart);
    if (tokenEnd < 0) tokenEnd = setCookie.length();
    String token = setCookie.substring(tokenStart, tokenEnd);
    token.trim();
    if (token.isEmpty()) {
        Serial.println("[WS] Session login failed: empty session token");
        return false;
    }

    _sessionCookie = cookiePrefix + token;
    String headers = "Origin: http://" + arenaHost + ":" + String(arenaPort)
                   + "\r\nCookie: " + _sessionCookie;
    _ws.setExtraHeaders(headers.c_str());
    _usingSessionAuth = true;
    Serial.println("[WS] Session login succeeded; cookie enabled for retry");
    return true;
}

// ─── Coil callback registration ──────────────────────────────────────────────

void WsManager::onCoilUpdate(CoilCallback cb) {
    _coilCb = cb;
}

// ─── Send helpers ─────────────────────────────────────────────────────────────

void WsManager::_sendJson(const String& type, JsonDocument& doc) {
    if (!_connected) {
        Serial.println("[WS] Not connected — message dropped");
        return;
    }
    String out;
    serializeJson(doc, out);
    _ws.sendTXT(out);
    //Serial.printf("[WS] → %s\n", out.c_str());
    WS_LOG("[WS] → %s\n", out.c_str());
}

void WsManager::sendInputs(const bool* states, uint8_t count) {
    if (_stopInputs) return; // Only the acknowledged stop path may write stop inputs.
    if (!sendInputsEnabled || !_connected) return;

    JsonDocument doc;
    doc["type"] = "setInput";     
    JsonArray arr = doc["data"].to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["channel"]  = i;
        o["state"] = states[i];
    }

    _sendJson("setInput", doc);
}
void WsManager::sendInput(const bool state, uint8_t channel) { // Single channel version for testing
    if (_stopInputs) return;
    if (!sendInputsEnabled || !_connected) return;

    JsonDocument doc;
    doc["type"] = "setInput";   
    JsonArray arr = doc["data"].to<JsonArray>();

    JsonObject o = arr.add<JsonObject>();
    o["channel"]  = channel;
    o["state"] = state;

    _sendJson("setInput", doc);
}

bool WsManager::sendStopInput(bool state, uint8_t channel) {
    if (!_stopInputs || channel < _firstStopChannel ||
        channel >= _firstStopChannel + _stopChannelCount ||
        !_connected || !_inputAck.begin(millis())) return false;
    String frame = "{\"type\":\"setInput\",\"data\":[{\"channel\":";
    frame += String(static_cast<unsigned int>(channel));
    frame += ",\"state\":";
    frame += state ? "true}]}" : "false}]}";
    if (!_ws.sendTXT(frame)) {
        _inputAck.reply(false);
        _ws.disconnect();
        _connected = false;
        _inputAck.disconnected();
    }
    // Accepted for tracking; a failed/partial write is consumed as Failed,
    // never treated as an acknowledgment of remote application.
    return true;
}

void WsManager::sendCounters(int64_t ch0, int64_t ch1,
                              int64_t ch2, int64_t ch3,
                              const RoleConfig& role) {
    if (_stopInputs || !sendRegistersEnabled || !_connected) return;

    int64_t total = ch0 + ch1 + ch2 + ch3;

    JsonDocument doc;
    doc["type"] = "setRegisters";
    JsonArray arr = doc["data"].to<JsonArray>();

    auto addReg = [&](uint8_t reg, int64_t val) {
        JsonObject o = arr.add<JsonObject>();
        o["register"] = reg;
        o["cValue"]   = (long)val;
    };

    addReg(role.regCh0,   ch0);
    addReg(role.regCh1,   ch1);
    addReg(role.regCh2,   ch2);
    addReg(role.regCh3,   ch3);
    addReg(role.regTotal, total);

    _sendJson("setRegisters", doc);
}

// ─── Inbound message handling ─────────────────────────────────────────────────

void WsManager::_handleCoilChange(JsonObject data) {
    // Payload shape: {"Coils":[false,true,false,...]}
    JsonArray coilArr = data["Coils"].as<JsonArray>();
    if (coilArr.isNull()) {
        Serial.println("[WS] plcIoChange: no Coils array found");
        return;
    }

    if (coilArr.size() > 128) { ++_parseErrors; return; }
    if (_fmsTable) {
        // A partial/malformed snapshot must not spuriously turn stack lights off.
        if (coilArr.size() <= COIL_STACK_LIGHT_BLUE) { ++_parseErrors; return; }
        for (uint8_t i = COIL_STACK_LIGHT_GREEN; i <= COIL_STACK_LIGHT_BLUE; ++i) {
            if (!coilArr[i].is<bool>()) { ++_parseErrors; return; }
        }
    }
    uint8_t count = coilArr.size();
    bool coils[128];
    for (uint8_t i = 0; i < count; i++) {
        coils[i] = coilArr[i].as<bool>();
    }

    // Print all coil states
    WS_LOG("[WS] Coils(%d):", count);
    for (uint8_t i = 0; i < count; i++) {
        WS_LOG("  [%d]=%s", i, coils[i] ? "ON" : "OFF");
    }
    WS_LOG(" ");

    // Fire registered callback
    if (_coilCb) _coilCb(coils, count);
}

void WsManager::_handleMessage(const String& raw) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, raw);
    if (err) {
        ++_parseErrors;
        WS_LOG("[WS] JSON parse error: %s\n", err.c_str());
        return;
    }

    if (!doc["type"].is<const char*>()) { ++_parseErrors; return; }
    ++_messages;
    String type = doc["type"].as<String>();
    JsonObject data = doc["data"].as<JsonObject>();

    // Avoid dumping high-rate LED frames when another LED source is active.
    bool suppressLog = type == "setLedMode" && !_ledModeEnabled;
    if (!suppressLog) {
        WS_LOG("[WS] ← Received JSON:");
        if (_debugSerial) {
            serializeJsonPretty(doc, Serial);
        }
        WS_LOG(" ");
    }

    if (type == "plcIoChange") {
        ++_plcMessages;
        WS_LOG("[WS] ← plcIoChange received\n");
        _handleCoilChange(data);
    } else if (type == "arenaStatus") {
        ++_arenaMessages;
        WS_LOG("[WS] ← arenaStatus received\n");
    } else if (type == "plcInputSetSuccess") {
        if (_stopInputs) {
            bool valid = InputAck::valid(data["success"].is<bool>(), data["success"].as<bool>(),
                                         data["count"].is<int>(), data["count"].as<int>());
            _inputAck.reply(valid);
        }
    } else if (type == "plcRegisterSetSuccess") {
        WS_LOG("[WS] ← Unhandled type: %s\n", type.c_str());
        WS_LOG("[WS] ← Register set ACK");
    } else if (type == "setLedMode") {
        _handleSetLedMode(data);
    } else if (type == "ping") {
        WS_LOG("[WS] ← Ping received\n");
    } else if (type == "error") {
        if (_stopInputs) _inputAck.reply(false);
        Serial.printf("[WS] ← Server error: %s\n",
                      doc["data"].as<String>().c_str());
    } else {
        WS_LOG("[WS] ← Unhandled type: %s\n", type.c_str());
    }
}

// ─── WebSocket event handler ──────────────────────────────────────────────────

void WsManager::_onEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: case WStype_TEXT: case WStype_BIN:
        case WStype_FRAGMENT_TEXT_START: case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT: case WStype_FRAGMENT_FIN:
        case WStype_PING: case WStype_PONG:
            _lastReceive = millis();
            if (_lastReceive == 0) _lastReceive = 1; // 0 means "nothing yet".
            break;
        default:
            break;
    }

    switch (type) {
        case WStype_CONNECTED:
            _connected = true;
            Serial.printf("[WS] Connected to %s:%d\n",
                          arenaHost.c_str(), arenaPort);
            break;

        case WStype_DISCONNECTED:
            _inputAck.disconnected();
            ++_disconnects;
            _connected = false;
            _receivingTextFragment = false;
            _fragmentBuffer = "";

            if (length > 0) {
                String reason(payload, length);
                Serial.printf("[WS] Disconnected: %s\n", reason.c_str());

                // Protected cheesy-arena routes redirect unauthenticated
                // requests to /login. Obtain a session cookie before retrying.
                bool authenticationRequired =
                    reason.indexOf("HTTP 401") >= 0 ||
                    reason.indexOf("HTTP 307") >= 0;
                // Stop roles use the inspected public /api/plc/websocket endpoint.
                // Never run synchronous login HTTP in its WebSocket callback.
                if (!_stopInputs && !_usingSessionAuth && authenticationRequired) {
                    Serial.println("[WS] Authentication required; requesting session cookie");
                    _authenticateWithSession();
                }
            } else {
                Serial.println("[WS] Disconnected — will retry");
            }
            break;

        case WStype_TEXT:
            if (length > WS_MAX_MESSAGE_SIZE) { ++_parseErrors; break; }
            _handleMessage(String(payload, length));
            break;

        case WStype_FRAGMENT_TEXT_START:
            _fragmentBuffer = "";
            _receivingTextFragment = true;
            _appendTextFragment(payload, length);
            break;

        case WStype_FRAGMENT:
            _appendTextFragment(payload, length);
            break;

        case WStype_FRAGMENT_FIN:
            if (_appendTextFragment(payload, length)) {
                _handleMessage(_fragmentBuffer);
            }
            _receivingTextFragment = false;
            _fragmentBuffer = "";
            break;

        case WStype_ERROR:
            Serial.println("[WS] Socket error");
            break;

        default:
            break;
    }
    
}

bool WsManager::_appendTextFragment(const uint8_t* payload, size_t length) {
    if (!_receivingTextFragment) return false;

    if (_fragmentBuffer.length() + length > WS_MAX_MESSAGE_SIZE) {
        Serial.printf("[WS] Fragmented message dropped: exceeds %u bytes\n",
                      (unsigned)WS_MAX_MESSAGE_SIZE);
        _receivingTextFragment = false;
        _fragmentBuffer = "";
        return false;
    }

    if (!_fragmentBuffer.concat(payload, length)) {
        Serial.println("[WS] Fragmented message dropped: insufficient memory");
        _receivingTextFragment = false;
        _fragmentBuffer = "";
        return false;
    }

    return true;
}

void WsManager::onSetLedMode(LedModeCallback cb) {
    _ledModeCb = cb;
}

void WsManager::setLedModeEnabled(bool enabled) {
    _ledModeEnabled = enabled;
}

void WsManager::_handleSetLedMode(JsonObject data) {
    if (!_ledModeEnabled) return;

    JsonVariant redModeValue = data["RedMode"];
    JsonVariant blueModeValue = data["BlueMode"];

    if (redModeValue.isNull() || blueModeValue.isNull()) {
        Serial.println("[WS] setLedMode ignored: missing RedMode or BlueMode");
        return;
    }

    int redMode = redModeValue.as<int>();
    int blueMode = blueModeValue.as<int>();
    if (redMode < 0 || redMode > 16 || blueMode < 0 || blueMode > 16) {
        Serial.printf("[WS] setLedMode ignored: invalid modes RedMode=%d BlueMode=%d\n",
                      redMode, blueMode);
        return;
    }

    WS_LOG("[WS] ← setLedMode received: RedMode=%d BlueMode=%d\n",
                  redMode, blueMode);

    if (_ledModeCb) _ledModeCb(redMode, blueMode);
}
