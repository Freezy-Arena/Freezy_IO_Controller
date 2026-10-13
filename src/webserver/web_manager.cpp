#include "web_manager.h"
#include "page_branding.h"
#include "input_status_page.h"
#include "../alliance/alliance_policy.h"
#include "../alliance/alliance_settings.h"

static String htmlEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length());
    for (size_t i = 0; i < value.length(); i++) {
        switch (value[i]) {
            case '&': escaped += "&amp;";  break;
            case '<': escaped += "&lt;";   break;
            case '>': escaped += "&gt;";   break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += value[i]; break;
        }
    }
    return escaped;
}

WebManager::WebManager(EthManager& eth, RoleManager& role, LedManager& leds, WsManager& ws)
    : _server(80), _eth(eth), _role(role), _leds(leds), _ws(ws) {}

void WebManager::begin() {
    _setupRoutes();
    _server.begin();
    Serial.printf("[WEB] Server started at http://%s\n",
                  _eth.localIPString().c_str());
}

// ─── Page builder ────────────────────────────────────────────────────────────
String WebManager::_buildPage(const String& message) {
    String ip     = _eth.staticIP;
    String gw     = _eth.staticGW;
    bool  isDHCP  = _eth.useDHCP;
    String wsHost = _ws.arenaHost;

    String html = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Device Configuration</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: #0f1117;
      color: #e2e8f0;
      min-height: 100vh;
      display: flex;
      align-items: flex-start;
      justify-content: center;
      padding: 40px 16px;
    }
    .card {
      background: #1a1d27;
      border: 1px solid #2d3148;
      border-radius: 12px;
      padding: 36px;
      width: 100%;
      max-width: 480px;
    }
    .header {
      display: flex;
      align-items: center;
      gap: 12px;
      margin-bottom: 28px;
    }
    .dot {
      width: 10px; height: 10px;
      border-radius: 50%;
      background: #22c55e;
      box-shadow: 0 0 8px #22c55e;
      flex-shrink: 0;
    }
    h1 { font-size: 1.2rem; font-weight: 600; letter-spacing: 0.02em; color: #f1f5f9; }
    .status-block {
      background: #12151f;
      border: 1px solid #2d3148;
      border-radius: 8px;
      padding: 14px 18px;
      margin-bottom: 28px;
      font-size: 0.85rem;
    }
    .status-row { display: flex; justify-content: space-between; padding: 4px 0; color: #94a3b8; }
    .status-row span:last-child { color: #e2e8f0; font-family: monospace; font-size: 0.9rem; }
    .message { padding: 12px 16px; border-radius: 8px; margin-bottom: 20px; font-size: 0.875rem; font-weight: 500; }
    .message.error  { background: #2d1515; border: 1px solid #7f1d1d; color: #fca5a5; }
    .message.success { background: #14281e; border: 1px solid #14532d; color: #86efac; }
    h2 { font-size: 0.8rem; font-weight: 600; text-transform: uppercase; letter-spacing: 0.08em; color: #64748b; margin-bottom: 18px; }
    .field { margin-bottom: 18px; }
    label { display: block; font-size: 0.85rem; color: #94a3b8; margin-bottom: 6px; }
    input[type="text"], input[type="number"], select {
      width: 100%;
      padding: 10px 14px;
      background: #12151f;
      border: 1px solid #2d3148;
      border-radius: 8px;
      color: #e2e8f0;
      font-size: 0.95rem;
      font-family: monospace;
      transition: border-color 0.2s;
      outline: none;
    }
    input[type="text"]:focus, input[type="number"]:focus, select:focus { border-color: #6366f1; }
    input[type="text"]:disabled { opacity: 0.4; cursor: not-allowed; }
    .toggle-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      background: #12151f;
      border: 1px solid #2d3148;
      border-radius: 8px;
      padding: 12px 16px;
      margin-bottom: 18px;
    }
    .toggle-label { font-size: 0.9rem; color: #e2e8f0; }
    .toggle-label small { display: block; font-size: 0.78rem; color: #64748b; margin-top: 2px; }
    .switch { position: relative; display: inline-block; width: 44px; height: 24px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; inset: 0; background: #2d3148; border-radius: 24px; cursor: pointer; transition: background 0.2s; }
    .slider::before { content: ''; position: absolute; width: 18px; height: 18px; left: 3px; bottom: 3px; background: #94a3b8; border-radius: 50%; transition: transform 0.2s, background 0.2s; }
    input:checked + .slider { background: #6366f1; }
    input:checked + .slider::before { transform: translateX(20px); background: #fff; }
    .divider { border: none; border-top: 1px solid #2d3148; margin: 24px 0; }
    button[type="submit"] {
      width: 100%; padding: 12px;
      background: #6366f1; color: #fff;
      border: none; border-radius: 8px;
      font-size: 0.95rem; font-weight: 600;
      cursor: pointer;
      transition: background 0.2s, opacity 0.2s;
      letter-spacing: 0.02em;
    }
    button[type="submit"]:hover { background: #4f46e5; }
    button[type="submit"]:active { opacity: 0.8; }
    .nav-link {
      display: block; text-align: center; text-decoration: none;
      padding: 10px 14px; margin-bottom: 18px;
      color: #c7d2fe; background: #20243a;
      border: 1px solid #373d64; border-radius: 8px;
      font-size: 0.9rem; font-weight: 600;
    }
    .nav-link:hover { background: #292e49; }
  </style>
  <script>
    function toggleStatic(checked) {
      var fields = document.querySelectorAll('.static-field');
      fields.forEach(function(f) {
        f.querySelector('input').disabled = checked;
        f.style.opacity = checked ? '0.4' : '1';
      });
    }
    window.onload = function() {
      var cb = document.getElementById('dhcpToggle');
      toggleStatic(cb.checked);
      cb.addEventListener('change', function() { toggleStatic(this.checked); });
    };
  </script>
</head>
<body>
  <div class="card">
    )" DEVICE_TITLE_BAR R"(
    <div class="header">
      <div class="dot"></div>
      <h1>Device Configuration</h1>
    </div>)";

    // Message banner
    if (message.length()) {
        bool isError = message.startsWith("ERROR");
        html += "<div class='message " + String(isError ? "error" : "success") + "'>"
                + message + "</div>";
    }

    // Status block
    html += R"(
    <div class="status-block">
      <div class="status-row"><span>IP Address</span><span>)"
    + _eth.localIPString() + R"(</span></div>
      <div class="status-row"><span>Mode</span><span>)"
    + String(isDHCP ? "DHCP" : "Static") + R"(</span></div>
    </div>

    <h2>Network Settings</h2>
    <form method="POST" action="/save">

      <div class="toggle-row">
        <div class="toggle-label">
          Use DHCP
          <small>Automatically assign IP address</small>
        </div>
        <label class="switch">
          <input type="checkbox" id="dhcpToggle" name="useDHCP" value="1" )"
    + String(isDHCP ? "checked" : "") + R"(>
          <span class="slider"></span>
        </label>
      </div>

      <div class="field static-field">
        <label>Static IP Address</label>
        <input type="text" name="staticIP" value=")" + ip + R"(" placeholder="192.168.1.200">
      </div>

      <div class="field static-field">
        <label>Gateway</label>
        <input type="text" name="staticGW" value=")" + gw + R"(" placeholder="192.168.1.1">
      </div>

      <hr class="divider">
      <h2>WebSocket Settings</h2>
      <div class="field">
        <label>Arena Server IP (WebSocket and FMS HTTP)</label>
        <input type="text" name="wsHost" value=")" + wsHost + R"(" placeholder="10.0.100.5">
        <label>Arena Server Port</label>
        <input type="number" name="wsPort" min="1" max="65535" value=")" + String(_ws.arenaPort) + R"(">
      </div>
      <a class="nav-link" href="/websocket">Configure WebSocket Messages</a>

      <hr class="divider">
      <h2>Device Role</h2>
      <div class="field">
        <label>Role</label>
        <select name="role">
          <option value="redHub" )"
    + String(_role.getRoleName() == "redHub"  ? "selected" : "") + R"(>Red Hub</option>
          <option value="blueHub" )"
    + String(_role.getRoleName() == "blueHub" ? "selected" : "") + R"(>Blue Hub</option>
          <option value="FMS_TABLE" )"
    + String(_role.getRoleName() == "FMS_TABLE" ? "selected" : "") + R"(>FMS Table</option>
          <option value="RED_ALLIANCE" )"
    + String(_role.getRoleName() == "RED_ALLIANCE" ? "selected" : "") + R"(>Red Alliance E-stop / A-stop</option>
          <option value="BLUE_ALLIANCE" )"
    + String(_role.getRoleName() == "BLUE_ALLIANCE" ? "selected" : "") + R"(>Blue Alliance E-stop / A-stop</option>
        </select>
      </div>

      <hr class="divider">
      <h2>Alliance Stop Inputs</h2>
      <div class="field">
        <label>E-stop / A-stop press debounce (ms)</label>
        <input type="number" name="pressDebounceMs" min="0" max="1000" step="1" value=")"
    + String(alliance::loadPressDebounceMs()) + R"(">
        <small>Red/Blue Alliance roles only. A press must remain stable for this duration.
        Releases have no debounce; the existing stop hold still applies.
        0 disables debounce. Applies after reboot.</small>
      </div>)";

    if (isStopRole(_role.getRole())) {
        html += "<a class=\"nav-link\" href=\"/inputs\">View Input Status</a>";
    }

    html += R"(

      <hr class="divider">
      <h2>LED Control</h2>
      <a class="nav-link" href="/led">Configure LED Settings</a>
      <a class="nav-link" href="/update">Firmware Update</a>

      <hr class="divider">
      <button type="submit">Save &amp; Reboot</button>
    </form>
  </div>
</body>
</html>)";

    return html;
}

// ─── Routes ──────────────────────────────────────────────────────────────────

String WebManager::_buildLedPage(const String& message) {
    uint8_t ledMode = static_cast<uint8_t>(_eth.ledControlMode);
    String colorOrder = _leds.getColorOrderName();

    String html = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>LED Settings</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; }
    body { margin: 0; padding: 40px 16px; min-height: 100vh; background: #0f1117;
      color: #e2e8f0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      display: flex; align-items: flex-start; justify-content: center; }
    .card { width: 100%; max-width: 480px; padding: 36px; background: #1a1d27;
      border: 1px solid #2d3148; border-radius: 12px; }
    h1 { margin: 0 0 8px; font-size: 1.2rem; color: #f1f5f9; }
    .intro { margin: 0 0 20px; color: #94a3b8; font-size: 0.85rem; line-height: 1.45; }
    .warning { padding: 12px 16px; margin-bottom: 20px; border: 1px solid #854d0e;
      border-radius: 8px; background: #2b2111; color: #fde68a; font-size: 0.85rem; line-height: 1.4; }
    .message { padding: 12px 16px; margin-bottom: 20px; border: 1px solid #7f1d1d;
      border-radius: 8px; background: #2d1515; color: #fca5a5; font-size: 0.875rem; }
    .field { margin-bottom: 18px; }
    label { display: block; margin-bottom: 6px; color: #cbd5e1; font-size: 0.85rem; }
    input, select { width: 100%; padding: 11px 12px; color: #e2e8f0; background: #12151f;
      border: 1px solid #2d3148; border-radius: 8px; font-size: 0.9rem; }
    input:focus, select:focus { outline: none; border-color: #6366f1; }
    button, .back { display: block; width: 100%; padding: 12px; border-radius: 8px;
      font-size: 0.95rem; font-weight: 600; text-align: center; }
    button { margin-top: 24px; border: 0; cursor: pointer; color: #fff; background: #6366f1; }
    button:hover { background: #4f46e5; }
    .back { margin-top: 12px; color: #94a3b8; text-decoration: none; }
    .back:hover { color: #e2e8f0; }
  </style>
</head>
<body>
  <div class="card">
    )" DEVICE_TITLE_BAR R"(
    <h1>LED Settings</h1>
    <p class="intro">Configure how this controller drives the connected LED strip.</p>
    <div class="warning"><strong>Reboot required:</strong> Saving this page will reboot the controller so all LED changes can take effect.</div>)";

    if (message.length()) {
        html += "<div class='message'>" + message + "</div>";
    }

    html += R"(
    <form method="POST" action="/led/save">
      <div class="field">
        <label for="ledControl">Control Mode</label>
        <select id="ledControl" name="ledControl">
          <option value="0" )" + String(ledMode == 0 ? "selected" : "") + R"(>Coil</option>
          <option value="1" )" + String(ledMode == 1 ? "selected" : "") + R"(>DMX Direct (Single Universe)</option>
          <option value="2" )" + String(ledMode == 2 ? "selected" : "") + R"(>DMX WebSocket (Hub role)</option>
        </select>
      </div>
      <div class="field">
        <label for="ledCount">Number of LEDs</label>
        <input id="ledCount" type="number" name="ledCount" value=")"
        + String(_leds.getLedCount()) + R"(" min="1" max=")"
        + String(_leds.getMaxLedCount()) + R"(" step="1" required>
      </div>
      <div class="field">
        <label for="colorOrder">RGB Color Order</label>
        <select id="colorOrder" name="colorOrder">
          <option value="RGB" )" + String(colorOrder == "RGB" ? "selected" : "") + R"(>RGB</option>
          <option value="RBG" )" + String(colorOrder == "RBG" ? "selected" : "") + R"(>RBG</option>
          <option value="GRB" )" + String(colorOrder == "GRB" ? "selected" : "") + R"(>GRB</option>
          <option value="GBR" )" + String(colorOrder == "GBR" ? "selected" : "") + R"(>GBR</option>
          <option value="BRG" )" + String(colorOrder == "BRG" ? "selected" : "") + R"(>BRG</option>
          <option value="BGR" )" + String(colorOrder == "BGR" ? "selected" : "") + R"(>BGR</option>
        </select>
      </div>
      <button type="submit">Save &amp; Reboot</button>
    </form>
    <a class="back" href="/">Back to Device Configuration</a>
  </div>
</body>
</html>)";

    return html;
}

String WebManager::_buildWebSocketPage(const String& message) {
    String html = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WebSocket Message Controls</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; }
    body { margin: 0; padding: 40px 16px; min-height: 100vh; background: #0f1117;
      color: #e2e8f0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      display: flex; align-items: flex-start; justify-content: center; }
    .card { width: 100%; max-width: 480px; padding: 36px; background: #1a1d27;
      border: 1px solid #2d3148; border-radius: 12px; }
    h1 { margin: 0 0 8px; font-size: 1.2rem; color: #f1f5f9; }
    .intro { margin: 0 0 24px; color: #94a3b8; font-size: 0.85rem; line-height: 1.45; }
    .message { padding: 12px 16px; margin-bottom: 20px; border: 1px solid #14532d;
      border-radius: 8px; background: #14281e; color: #86efac; font-size: 0.875rem; }
    .toggle-row { display: flex; align-items: center; justify-content: space-between;
      padding: 14px 16px; margin-bottom: 16px; background: #12151f;
      border: 1px solid #2d3148; border-radius: 8px; }
    .toggle-label { font-size: 0.9rem; }
    .toggle-label small { display: block; margin-top: 3px; color: #64748b; font-size: 0.76rem; }
    .field { margin-bottom: 16px; }
    .field label { display: block; margin-bottom: 6px; color: #cbd5e1; font-size: 0.85rem; }
    .field input { width: 100%; padding: 11px 12px; color: #e2e8f0; background: #12151f;
      border: 1px solid #2d3148; border-radius: 8px; font-size: 0.9rem; }
    .field input:focus { outline: none; border-color: #6366f1; }
    .switch { position: relative; display: inline-block; width: 44px; height: 24px; flex-shrink: 0; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; inset: 0; cursor: pointer; background: #2d3148;
      border-radius: 24px; transition: background 0.2s; }
    .slider::before { content: ''; position: absolute; width: 18px; height: 18px;
      left: 3px; bottom: 3px; background: #94a3b8; border-radius: 50%;
      transition: transform 0.2s, background 0.2s; }
    input:checked + .slider { background: #6366f1; }
    input:checked + .slider::before { transform: translateX(20px); background: #fff; }
    button, .back { display: block; width: 100%; padding: 12px; border-radius: 8px;
      font-size: 0.95rem; font-weight: 600; text-align: center; }
    button { margin-top: 24px; border: 0; cursor: pointer; color: #fff; background: #6366f1; }
    button:hover { background: #4f46e5; }
    .back { margin-top: 12px; color: #94a3b8; text-decoration: none; }
    .back:hover { color: #e2e8f0; }
  </style>
</head>
<body>
  <div class="card">
    )" DEVICE_TITLE_BAR R"(
    <h1>WebSocket Message Controls</h1>
    <p class="intro">Choose which PLC update messages this controller may transmit. Incoming WebSocket messages are unaffected.</p>)";

    if (message.length()) {
        html += "<div class='message'>" + message + "</div>";
    }

    html += R"(
    <form method="POST" action="/websocket/save">
      <div class="field">
        <label for="authUsername">Arena username</label>
        <input id="authUsername" name="authUsername" type="text" autocomplete="username" value=")"
        + htmlEscape(_ws.authUsername) + R"(" required>
      </div>
      <div class="field">
        <label for="authPassword">Arena password</label>
        <input id="authPassword" name="authPassword" type="password" autocomplete="current-password" value=")"
        + htmlEscape(_ws.authPassword) + R"(" required>
      </div>
      <div class="toggle-row">
        <div class="toggle-label">Send Registers
          <small>Allow hub counter updates</small>
        </div>
        <label class="switch">
          <input type="checkbox" name="sendRegisters" value="1" )"
        + String(_ws.sendRegistersEnabled ? "checked" : "") + R"(>
          <span class="slider"></span>
        </label>
      </div>
      <div class="toggle-row">
        <div class="toggle-label">Send Inputs
          <small>Allow hub sensor updates. E-stop and A-stop delivery always stays enabled.</small>
        </div>
        <label class="switch">
          <input type="checkbox" name="sendInputs" value="1" )"
        + String(_ws.sendInputsEnabled ? "checked" : "") + R"(>
          <span class="slider"></span>
        </label>
      </div>
      <button type="submit">Save WebSocket Settings</button>
    </form>
    <a class="back" href="/">Back to Device Configuration</a>
  </div>
</body>
</html>)";

    return html;
}

void WebManager::_setupRoutes() {
    _setupOtaRoutes();

    _server.on("/inputs", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (!isStopRole(_role.getRole())) {
            req->send(404, "text/plain", "Input status is available for FMS Table and alliance roles after saving and rebooting.");
            return;
        }
        auto* response = req->beginResponse(200, "text/html", INPUT_STATUS_PAGE);
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
            req->send(200, "text/html", "<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Alliance Stop LEDs</title><body style='background:#0f1117;color:#e2e8f0;font-family:system-ui;padding:32px 16px'><main style='max-width:520px;margin:auto'>" DEVICE_TITLE_BAR "<h1>Alliance Stop LEDs</h1><p>Status indicators use the original stop-controller wiring: GPIO 47, GRB, 750 LEDs, brightness 15, 900 mW power limit.</p><p>LED 0: delivery heartbeat; LED 1: alliance color (flashing red on a latched fault); LED 2: WebSocket activity. Other pixels remain off.</p><p><a href='/'>Back to configuration</a></p></main></body></html>");
            return;
        }
        if (_role.getRole() == ROLE_FMS_TABLE) {
            req->send(200, "text/html", "<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>FMS Table LEDs</title><body style='background:#0f1117;color:#e2e8f0;font-family:system-ui;padding:32px 16px'><main style='max-width:520px;margin:auto'>" DEVICE_TITLE_BAR "<h1>FMS Table LEDs</h1><p>Stack lights and status indicators use the table wiring: GPIO 47, GRB, 750 LEDs, brightness 15, 900 mW power limit.</p><p><a href='/'>Back to configuration</a></p></main></body></html>");
            return;
        }
        req->send(200, "text/html", _buildLedPage());
    });

    _server.on("/led/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (_otaRequest || _rebootPending) { req->send(409, "text/plain", "Update or reboot in progress."); return; }
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
        _eth.savePreferences();
        _leds.savePreferences();

        Serial.printf("[WEB] LED settings saved - mode: %d, count: %d, color order: %s\n",
                      mode, count, order.c_str());

        String rebootPage = R"(<!DOCTYPE html><html><head><meta charset="UTF-8">
        <meta http-equiv="refresh" content="5;url=/"><title>Rebooting</title></head>
        <body style="background:#0f1117;color:#e2e8f0;font-family:sans-serif;
        display:flex;align-items:center;justify-content:center;height:100vh;margin:0;">
        <div style="text-align:center;">)" DEVICE_TITLE_BAR R"(<h2 style="color:#86efac;">LED Settings Saved</h2>
        <p style="color:#94a3b8;">Device is rebooting &mdash; reconnecting in 5 seconds...</p>
        </div></body></html>)";
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

        String rebootPage = R"(<!DOCTYPE html>
        <html><head>
        <meta charset="UTF-8">
        <meta http-equiv="refresh" content="5;url=/">
        <title>Rebooting</title>
        </head><body style="background:#0f1117;color:#e2e8f0;font-family:sans-serif;
        display:flex;align-items:center;justify-content:center;height:100vh;margin:0;">
        <div style="text-align:center;">)" DEVICE_TITLE_BAR R"(
          <h2 style="color:#86efac;">Settings Saved</h2>
          <p style="color:#94a3b8;">Device is rebooting — reconnecting in 5 seconds...</p>
        </div>
        </body></html>)";

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
