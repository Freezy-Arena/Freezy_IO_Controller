#include <Arduino.h>
#include "led/led_manager.h"
#include "led/winner_display.h"
#include "counter/counter_manager.h"
#include "relay/relay_manager.h"
#include "network/network_manager.h"
#include "webserver/web_manager.h"
#include "websocket/ws_manager.h"
#include "websocket/coil_map.h"
#include "websocket/input_map.h"
#include "dmx_led/dmx_led_manager.h"
#include "led_animator/led_animator.h"
#include "config/legacy_fms.h"
#include "fms_table/fms_table.h"
#include "alliance/alliance_stops.h"

LedManager leds;
WinnerDisplay winnerDisplay;
CounterManager counters;
RelayManager relays;
EthManager network;
RoleManager     roleManager;
WsManager       ws;
WebManager      web(network, roleManager, leds, ws);       // Pass managers so web can read/write prefs
DmxLedManager   dmxLed(leds, roleManager);
LedAnimator     ledAnimator(leds, roleManager);
FmsTable        fmsTable;
AllianceStops   allianceStops;
bool            isFmsTable = false; // Hardware role stays fixed until reboot.
bool            isAlliance = false;
bool            isStopController = false;

#define DEBUG_SERIAL false           // Routine debug only; keep summaries, connection events, and errors.
bool _debugSerial = DEBUG_SERIAL;

InputStatusSnapshot readInputStatus() {
    if (isFmsTable) return fmsTable.inputStatus();
    if (isAlliance) return allianceStops.inputStatus();
    return {};
}

// ─── Coil callback ────────────────────────────────────────────────────────────
// Fired by WsManager whenever a plcIoChange arrives

void onCoilUpdate(const bool* coils, uint8_t count) {
    const RoleConfig& role = roleManager.getConfig();
    if (isAlliance) {
        if (count > COIL_RED_WINS && count > COIL_BLUE_WINS) {
            winnerDisplay.observe(coils[COIL_RED_WINS], coils[COIL_BLUE_WINS], millis());
        }
        return;
    }
    if (isFmsTable) {
        fmsTable.onCoilUpdate(coils, count);
        return;
    }

    // Safety check — guard against shorter-than-expected coil arrays
    auto coilActive = [&](uint8_t c) -> bool {
        return c < count && coils[c];
    };

    // Ignore incomplete winner snapshots rather than inventing falling edges.
    if (count > COIL_RED_WINS && count > COIL_BLUE_WINS) {
        winnerDisplay.observe(coils[COIL_RED_WINS], coils[COIL_BLUE_WINS], millis());
        leds.setWinnerOverride(winnerDisplay.update(millis()),
            winnerDisplay.showRed(role.role == ROLE_RED_HUB) ? CRGB::Red : CRGB::Blue);
    }

    // Match reset → clear counters once on the coil's rising edge.
    static bool matchResetWasActive = false;
    if (COIL_MATCH_RESET < count) {
        const bool matchResetActive = coils[COIL_MATCH_RESET];
        const bool matchResetRising = matchResetActive && !matchResetWasActive;
        matchResetWasActive = matchResetActive;
        if (matchResetRising) {
        WS_PRINTLN("[MAIN] Match reset → clearing counters");
        counters.resetAll();
        }
    }

   // Role-specific relay and LED logic
    if (coilActive(role.coilMotor)) {
        relays.setState(0, true);
        relays.setState(1, true);
    } else {
        relays.setState(0, false);
        relays.setState(1, false);
    }

    // LED only in coil mode
    if (network.ledControlMode != LED_CONTROL_COIL) return;

    if (coilActive(role.coilLight)) {
        // Color per role
        if (role.role == ROLE_RED_HUB) {
            leds.showSolid(CRGB::Red);
        } else if (role.role == ROLE_BLUE_HUB) {
            leds.showSolid(CRGB::Blue);
        }
    } else if (coilActive(COIL_FIELD_RESET_LIGHT)) { // Only turn off if not also active for motor
        leds.showSolid(CRGB::Green);
    } else {
        leds.showSolid(CRGB::Black);
    }
}

void onSetLedMode(int redMode, int blueMode) {
    if (isStopController) return;
    if (network.ledControlMode != LED_CONTROL_WEBSOCKET) return;

    WS_LOG("[HUB] LED modes received: RedMode=%d BlueMode=%d\n",
                  redMode, blueMode);

    ledAnimator.setMode(static_cast<LedMode>(redMode),
                        static_cast<LedMode>(blueMode));
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("[BOOT] Starting...");

    migrateLegacyFmsSettings();
    roleManager.begin();            // Load role before anything that needs it
    isFmsTable = roleManager.getRole() == ROLE_FMS_TABLE;
    isAlliance = isAllianceRole(roleManager.getRole());
    isStopController = isStopRole(roleManager.getRole());
    ws.loadPreferences();
    if (isFmsTable) fmsTable.begin(ws.arenaHost, ws.arenaPort, network);
    if (isAlliance) allianceStops.begin(roleManager.getRole());

    const RoleConfig& role = roleManager.getConfig();

    if (!isStopController) {
        counters.begin();
        for (uint8_t i = 0; i < 4; i++) {
            counters.addChannel(i, role.counterPin[i]);
        }
        counters.startTask();                   //must be called after all channels added

        relays.begin();
        relays.addChannel(0, role.relayMotor); // Horizontal hub motor relay
        relays.addChannel(1, role.relayLight); // Vertical hub motor relay
    }

    leds.begin(isStopController);
    if (roleManager.getRole() != ROLE_FMS_TABLE) {
        winnerDisplay.configure(leds.getWinnerDelaySeconds() * 1000U,
                                leds.getWinnerDurationSeconds() * 1000U);
    }
    if (role.winnerPin != GPIO_NUM_NC) {
        pinMode(role.winnerPin, OUTPUT);
        digitalWrite(role.winnerPin, LOW);
    }
    ledAnimator.begin();

    network.begin(isStopController);
    web.setInputStatusProvider(readInputStatus);
    web.begin();                    // Also starts while waiting for Ethernet IP

     // Start WebSocket — prefs loaded inside begin()
    ws.onCoilUpdate(onCoilUpdate);
    ws.onSetLedMode(onSetLedMode);
    ws.configureStopRole(roleManager.getRole());
    ws.begin("", 0);                // Empty = use stored prefs

    if (!isStopController) dmxLed.begin();
}

void loop()
{
    network.update();
    // Print the IP periodically during the first 30 seconds of loop().
    static const uint32_t startupAt = millis();
    static uint32_t lastIpPrint = 0;

    const uint32_t now = millis();

    if (uint32_t(now - startupAt) < 30000 &&
        network.isConnected() &&
        uint32_t(now - lastIpPrint) >= 2000) {
        lastIpPrint = now;
        Serial.printf("[BOOT] Board address: http://%s\n",
                    network.localIPString().c_str());
    }
    ws.setLedModeEnabled(!isStopController && network.ledControlMode == LED_CONTROL_WEBSOCKET);
    ws.update();                    // Must be called every loop
    if (isFmsTable) fmsTable.serviceStops(ws);
    if (isAlliance) allianceStops.serviceStops(ws, network.isConnected());
    web.update();               // Handles pending reboot

    if (isAlliance) {
        const RoleConfig& role = roleManager.getConfig();
        const bool winnerActive = winnerDisplay.update(millis());
        if (role.winnerPin != GPIO_NUM_NC) {
            const bool redAlliance = role.role == ROLE_RED_ALLIANCE;
            digitalWrite(role.winnerPin,
                winnerActive && winnerDisplay.activeForAlliance(redAlliance) ? HIGH : LOW);
        }
        allianceStops.update(leds, ws.messageCount());
        delay(1);
        return;
    }

    if (isFmsTable) {
        fmsTable.update(leds, ws.messageCount());
        delay(1); // Yield to RTOS; stop sampling has its own higher-priority task.
        return;   // No hub relay, counter, DMX, or 500 ms input telemetry on table pins.
    }

    LedControlMode ledMode = network.ledControlMode;
    leds.setWinnerOverride(winnerDisplay.update(millis()),
        winnerDisplay.showRed(roleManager.getRole() == ROLE_RED_HUB) ? CRGB::Red : CRGB::Blue);

    // DMX direct — handled by dmxLed.update()
    if (ledMode == LED_CONTROL_DMX) {
        dmxLed.update();
    }
    // WebSocket mode animations are selected by setLedMode and rendered locally.
    if (ledMode == LED_CONTROL_WEBSOCKET) {
        ledAnimator.update();
    }

    // Coil mode — handled in onCoilUpdate
    // Only pass coil LED updates through if in coil mode

    // Send input states every 500ms
    static uint32_t lastInputSend = 0;
    if (millis() - lastInputSend >= 500) {
        lastInputSend = millis();
        const RoleConfig& role = roleManager.getConfig();
        bool state = digitalRead(role.counterPin[0]);  // Same pin as counter channel 0
        ws.sendInput(state, role.plcInputSensor[0]); // Map to role-specific input index
        // TODO: loop trhough all role-specific inputs and send as batch
        // create a list of couterpins to interate through

        
    }

    // Push counter values to arena server every 500ms
    static uint32_t lastSend = 0;
    if (millis() - lastSend >= 500) {
        lastSend = millis();
        ws.sendCounters(
            counters.getCount(0),
            counters.getCount(1),
            counters.getCount(2),
            counters.getCount(3),
            roleManager.getConfig()
            
        );
        
    }
  

    // Keep the status summary every 5 seconds, even with debug disabled.
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 5000) {
        lastPrint = millis();
        Serial.printf("[STATUS] Role:%s  Ch0:%lld Ch1:%lld Ch2:%lld Ch3:%lld | Relay:%s | WS:%s\n",
                      roleManager.getRoleName().c_str(),
                      counters.getCount(0), counters.getCount(1),
                      counters.getCount(2), counters.getCount(3),
                      relays.getState(0) ? "ON"  : "OFF",
                      ws.isConnected()   ? "UP"  : "DOWN");
    }
}
