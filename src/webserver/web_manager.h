#pragma once
#include <Arduino.h>
#include <atomic>
#include <esp_ota_ops.h>
#include <ESPAsyncWebServer.h>
#include "../network/network_manager.h"
#include "../role_config.h"
#include "../led/led_manager.h"
#include "../websocket/ws_manager.h"
#include "../input_status.h"

class WebManager {
public:
    WebManager(EthManager& eth, RoleManager& role, LedManager& leds, WsManager& ws);
    void begin();
    void update();
    // Set once before begin(); the provider returns a synchronized sampler snapshot.
    void setInputStatusProvider(InputStatusProvider provider) { _inputStatus = provider; }

private:
    AsyncWebServer  _server;
    EthManager&     _eth;
    RoleManager&    _role;
    LedManager&     _leds;
    WsManager&      _ws;
    InputStatusProvider _inputStatus = nullptr;

    std::atomic<bool> _rebootPending{false};
    uint32_t    _rebootAt       = 0;
    AsyncWebServerRequest* _otaRequest = nullptr;
    esp_ota_handle_t _otaHandle = 0;
    bool _otaWriting = false;
    size_t _otaReceived = 0;
    String _otaError;
    String _otaToken;
    void _setupOtaRoutes();

    String _buildPage(const String& message = "");
    String _buildWebSocketPage(const String& message = "");
    String _buildLedPage(const String& message = "");
    void   _setupRoutes();
};
