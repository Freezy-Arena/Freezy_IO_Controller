#include "web_manager.h"
#include "web_assets.h"
#include "../alliance/alliance_policy.h"
#include "../alliance/alliance_settings.h"

WebManager::WebManager(EthManager& eth, RoleManager& role, LedManager& leds, WsManager& ws)
    : _server(80), _eth(eth), _role(role), _leds(leds), _ws(ws) {}

void WebManager::begin() {
    _setupRoutes();
    _server.begin();
    Serial.printf("[WEB] Server started at http://%s\n",
                  _eth.localIPString().c_str());
}

String WebManager::_buildPage(const String& message) {
    String ip     = _eth.staticIP;
    String gw     = _eth.staticGW;
    bool  isDHCP  = _eth.useDHCP;
    String wsHost = _ws.arenaHost;

    return WebAssets::render("config", {
        {"ipAddress", _eth.localIPString()},
        {"networkMode", String(isDHCP ? "DHCP" : "Static")},
        {"dhcpChecked", String(isDHCP ? "checked" : "")},
        {"staticIp", ip},
        {"gateway", gw},
        {"arenaHost", wsHost},
        {"arenaPort", String(_ws.arenaPort)},
        {"role_redHub", String(_role.getRoleName() == "redHub" ? "selected" : "")},
        {"role_blueHub", String(_role.getRoleName() == "blueHub" ? "selected" : "")},
        {"role_FMS_TABLE", String(_role.getRoleName() == "FMS_TABLE" ? "selected" : "")},
        {"role_RED_ALLIANCE", String(_role.getRoleName() == "RED_ALLIANCE" ? "selected" : "")},
        {"role_BLUE_ALLIANCE", String(_role.getRoleName() == "BLUE_ALLIANCE" ? "selected" : "")},
        {"pressDebounceMs", String(alliance::loadPressDebounceMs())},
        {"inputStatusHidden", String(isStopRole(_role.getRole()) ? "" : "hidden")},
        {"message", message},
        {"messageHidden", String(message.isEmpty() ? "hidden" : "")},
        {"messageClass", String(message.startsWith("ERROR") ? "error" : "success")}
    });
}

String WebManager::_buildLedPage(const String& message) {
    uint8_t ledMode = static_cast<uint8_t>(_eth.ledControlMode);
    String colorOrder = _leds.getColorOrderName();

    return WebAssets::render("led", {
        {"mode_0", String(ledMode == 0 ? "selected" : "")},
        {"mode_1", String(ledMode == 1 ? "selected" : "")},
        {"mode_2", String(ledMode == 2 ? "selected" : "")},
        {"ledCount", String(_leds.getLedCount())},
        {"maxLedCount", String(_leds.getMaxLedCount())},
        {"winnerDelaySeconds", String(_leds.getWinnerDelaySeconds())},
        {"winnerDurationSeconds", String(_leds.getWinnerDurationSeconds())},
        {"maxWinnerSeconds", String(LedManager::MaxWinnerSeconds)},
        {"order_RGB", String(colorOrder == "RGB" ? "selected" : "")},
        {"order_RBG", String(colorOrder == "RBG" ? "selected" : "")},
        {"order_GRB", String(colorOrder == "GRB" ? "selected" : "")},
        {"order_GBR", String(colorOrder == "GBR" ? "selected" : "")},
        {"order_BRG", String(colorOrder == "BRG" ? "selected" : "")},
        {"order_BGR", String(colorOrder == "BGR" ? "selected" : "")},
        {"message", message},
        {"messageHidden", String(message.isEmpty() ? "hidden" : "")},
        {"messageClass", String(message.startsWith("ERROR") ? "error" : "success")}
    });
}

String WebManager::_buildWebSocketPage(const String& message) {
    return WebAssets::render("websocket", {
        {"authUsername", _ws.authUsername},
        {"authPassword", _ws.authPassword},
        {"registersChecked", String(_ws.sendRegistersEnabled ? "checked" : "")},
        {"inputsChecked", String(_ws.sendInputsEnabled ? "checked" : "")},
        {"message", message},
        {"messageHidden", String(message.isEmpty() ? "hidden" : "")},
        {"messageClass", String(message.startsWith("ERROR") ? "error" : "success")}
    });
}

void WebManager::_setupRoutes() {
    WebAssets::registerStaticRoutes(_server);
    _setupOtaRoutes();

    _server.on("/inputs", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (!isStopRole(_role.getRole())) {
            req->send(404, "text/plain", "Input status is available for FMS Table and alliance roles after saving and rebooting.");
            return;
        }
        auto* response = req->beginResponse(200, "text/html", WebAssets::render("inputs"));
        response->addHeader("Cache-Control", "no-store");
        req->send(response);
    });

    _server.on("/api/inputs", HTTP_GET, [this](AsyncWebServerRequest* req) {
        auto sendJson = [&](int code, const String& body) {
            auto* response = req->beginResponse(code, "application/json", body);
            response->addHeader("Cache-Control", "no-store");
            req->send(response);
        };
        if (!isStopRole(_role.getRole())) {
            sendJson(404, "{\"error\":\"Input status is not available for this role\"}");
            return;
        }
        if (!_inputStatus) {
            sendJson(503, "{\"error\":\"Input sampler unavailable\"}");
            return;
        }
        const InputStatusSnapshot snapshot = _inputStatus();
        bool table = _role.getRole() == ROLE_FMS_TABLE;
        uint8_t expectedCount = table ? 2 : alliance::InputCount;
        if (snapshot.count != expectedCount) {
            sendJson(503, "{\"error\":\"Input sampler unavailable\"}");
            return;
        }
        JsonDocument doc;
        doc["role"] = _role.getRoleName();
        doc["sampled"] = snapshot.sampled;
        doc["sampleAgeMs"] = uint32_t(millis() - snapshot.sampledAt);
        doc["fault"] = snapshot.fault;
        JsonArray inputs = doc["inputs"].to<JsonArray>();
        for (uint8_t i = 0; i < snapshot.count; ++i) {
            JsonObject input = inputs.add<JsonObject>();
            input["pressed"] = snapshot.pressed[i];
            input["kind"] = table && i == 1 ? "start" : "stop";
            if (table) {
                input["label"] = i == 0 ? "Field E-stop" : "Start button";
                input["pin"] = i == 0 ? fms::StopPin : fms::StartPin;
            } else {
                input["label"] = "Station " + String(i / 2 + 1) + (i % 2 == 0 ? " E-stop" : " A-stop");
                input["pin"] = alliance::Pins[i];
            }
        }
        String body;
        serializeJson(doc, body); // No sampler lock is held while allocating or serving HTTP.
        sendJson(200, body);
    });

    // GET / — config page
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
        req->send(200, "text/html", _buildPage());
    });

    _server.on("/websocket", HTTP_GET, [this](AsyncWebServerRequest* req) {
        req->send(200, "text/html", _buildWebSocketPage());
    });

    _server.on("/led", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (isAllianceRole(_role.getRole())) {
            req->send(200, "text/html", WebAssets::render("allianceLeds", {
                {"winnerDelaySeconds", String(_leds.getWinnerDelaySeconds())},
                {"winnerDurationSeconds", String(_leds.getWinnerDurationSeconds())},
                {"maxWinnerSeconds", String(LedManager::MaxWinnerSeconds)},
                {"message", ""}, {"messageHidden", "hidden"}, {"messageClass", "success"}
            }));
            return;
        }
        if (_role.getRole() == ROLE_FMS_TABLE) {
            req->send(200, "text/html", WebAssets::render("fmsLeds"));
            return;
        }
        req->send(200, "text/html", _buildLedPage());
    });

    _server.on("/led/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (_otaRequest || _rebootPending) { req->send(409, "text/plain", "Update or reboot in progress."); return; }
        if (isAllianceRole(_role.getRole())) {
            auto seconds = [&](const char* name, uint32_t& result) {
                if (!req->hasParam(name, true)) return false;
                const String text = req->getParam(name, true)->value();
                if (text.isEmpty() || text.length() > 4) return false;
                result = 0;
                for (size_t i = 0; i < text.length(); ++i) {
                    if (text[i] < '0' || text[i] > '9') return false;
                    result = result * 10 + (text[i] - '0');
                }
                return result <= LedManager::MaxWinnerSeconds;
            };
            uint32_t winnerDelay = 0, winnerDuration = 0;
            if (!seconds("winnerDelaySeconds", winnerDelay) ||
                !seconds("winnerDurationSeconds", winnerDuration)) {
                req->send(400, "text/html", WebAssets::render("allianceLeds", {
                    {"winnerDelaySeconds", String(_leds.getWinnerDelaySeconds())},
                    {"winnerDurationSeconds", String(_leds.getWinnerDurationSeconds())},
                    {"maxWinnerSeconds", String(LedManager::MaxWinnerSeconds)},
                    {"message", "ERROR: Winner delay and duration must be whole seconds from 0 to 3600."},
                    {"messageHidden", ""}, {"messageClass", "error"}
                }));
                return;
            }
            _leds.setWinnerTiming(winnerDelay, winnerDuration);
            _leds.saveWinnerTimingPreferences();
            req->send(200, "text/html", WebAssets::render("reboot", {{"heading", "Winner Strobe Timing Saved"}}));
            _rebootAt = millis() + 3000;
            _rebootPending = true;
            return;
        }
        if (isStopRole(_role.getRole())) {
            req->send(400, "text/plain", "Stop-controller roles use their fixed LED layouts.");
            return;
        }
        int mode = req->hasParam("ledControl", true)
            ? req->getParam("ledControl", true)->value().toInt() : -1;
        int count = req->hasParam("ledCount", true)
            ? req->getParam("ledCount", true)->value().toInt() : -1;
        String order = req->hasParam("colorOrder", true)
            ? req->getParam("colorOrder", true)->value() : "";
        // Strict whole-second parsing; reject invalid values before any mutation.
        auto seconds = [&](const char* name, uint32_t& result) {
            if (!req->hasParam(name, true)) return false;
            const String text = req->getParam(name, true)->value();
            if (text.isEmpty() || text.length() > 4) return false;
            result = 0;
            for (size_t i = 0; i < text.length(); ++i) {
                if (text[i] < '0' || text[i] > '9') return false;
                result = result * 10 + (text[i] - '0');
            }
            return result <= LedManager::MaxWinnerSeconds;
        };
        uint32_t winnerDelay = 0, winnerDuration = 0;
        if (!seconds("winnerDelaySeconds", winnerDelay) ||
            !seconds("winnerDurationSeconds", winnerDuration)) {
            req->send(400, "text/html", _buildLedPage("ERROR: Winner delay and duration must be whole seconds from 0 to 3600."));
            return;
        }

        if (mode < LED_CONTROL_COIL || mode > LED_CONTROL_WEBSOCKET) {
            req->send(200, "text/html", _buildLedPage("ERROR: Invalid LED control mode."));
            return;
        }
        if (count < 1 || count > _leds.getMaxLedCount()) {
            req->send(200, "text/html",
                      _buildLedPage(String("ERROR: LED count must be between 1 and ")
                                    + String(_leds.getMaxLedCount()) + "."));
            return;
        }
        if (!_leds.setColorOrderByName(order)) {
            req->send(200, "text/html", _buildLedPage("ERROR: Invalid RGB color order."));
            return;
        }

        _eth.ledControlMode = static_cast<LedControlMode>(mode);
        _leds.setLedCount(static_cast<uint16_t>(count));
        _leds.setWinnerTiming(winnerDelay, winnerDuration);
        _eth.savePreferences();
        _leds.savePreferences();

        Serial.printf("[WEB] LED settings saved - mode: %d, count: %d, color order: %s\n",
                      mode, count, order.c_str());

        String rebootPage = WebAssets::render("reboot", {{"heading", "LED Settings Saved"}});
        req->send(200, "text/html", rebootPage);
        _rebootAt = millis() + 3000;
        _rebootPending = true;
    });

    _server.on("/websocket/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (_otaRequest || _rebootPending) { req->send(409, "text/plain", "Update or reboot in progress."); return; }
        if (req->hasParam("authUsername", true))
            _ws.authUsername = req->getParam("authUsername", true)->value();
        if (req->hasParam("authPassword", true))
            _ws.authPassword = req->getParam("authPassword", true)->value();
        _ws.sendRegistersEnabled = req->hasParam("sendRegisters", true);
        _ws.sendInputsEnabled = req->hasParam("sendInputs", true);
        _ws.resetSessionAuth();
        _ws.savePreferences();

        Serial.printf("[WEB] WebSocket sends: setRegisters=%s, setInput=%s\n",
                      _ws.sendRegistersEnabled ? "enabled" : "disabled",
                      _ws.sendInputsEnabled ? "enabled" : "disabled");
        req->send(200, "text/html", _buildWebSocketPage("WebSocket settings saved."));
    });

    // POST /save — save and reboot
    _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (_otaRequest || _rebootPending) { req->send(409, "text/plain", "Update or reboot in progress."); return; }

        // Validate the entire form before changing persisted settings.
        auto value = [&](const char* key, const String& fallback) -> String {
            return req->hasParam(key, true) ? req->getParam(key, true)->value() : fallback;
        };
        bool dhcp = req->hasParam("useDHCP", true);
        String ip = value("staticIP", _eth.staticIP);
        String gateway = value("staticGW", _eth.staticGW);
        String host = value("wsHost", _ws.arenaHost);
        String role = value("role", _role.getRoleName());
        host.trim();
        long port = value("wsPort", String(_ws.arenaPort)).toInt();
        String debounceText = value("pressDebounceMs", String(alliance::loadPressDebounceMs()));
        bool validDebounce = debounceText.length() > 0 && debounceText.length() <= 4;
        for (size_t i = 0; i < debounceText.length(); ++i) {
            if (debounceText[i] < '0' || debounceText[i] > '9') validDebounce = false;
        }
        uint32_t debounceMs = debounceText.toInt();
        if (!validDebounce || debounceMs > alliance::MaxPressDebounceMs) {
            req->send(400, "text/plain", "Press debounce must be a whole number from 0 to 1000 ms; settings not saved.");
            return;
        }
        IPAddress address;
        if ((!dhcp && (!address.fromString(ip) || !address.fromString(gateway))) ||
            !address.fromString(host) || port < 1 || port > 65535 ||
            (role != "redHub" && role != "blueHub" && role != "FMS_TABLE" &&
             role != "RED_ALLIANCE" && role != "BLUE_ALLIANCE")) {
            req->send(400, "text/plain", "Invalid network address, port, or device role; settings not saved.");
            return;
        }
        if (!alliance::savePressDebounceMs(debounceMs)) {
            req->send(500, "text/plain", "Could not save press debounce; please retry.");
            return;
        }
        _eth.useDHCP = dhcp;
        _eth.staticIP = ip;
        _eth.staticGW = gateway;
        _ws.arenaHost = host;
        _ws.arenaPort = port;
        _ws.savePreferences();
        // Hardware role and HTTP worker destination stay fixed until reboot.
        _role.setRoleByName(role, false);
        _eth.savePreferences();

        String rebootPage = WebAssets::render("reboot", {{"heading", "Settings Saved"}});

        req->send(200, "text/html", rebootPage);
        _rebootAt       = millis() + 3000;
        _rebootPending  = true;
    });

    // 404
    _server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });

}

void WebManager::update() {
    if (_rebootPending && int32_t(millis() - _rebootAt) >= 0) {
        Serial.println("[WEB] Rebooting now...");
        ESP.restart();
    }
}
