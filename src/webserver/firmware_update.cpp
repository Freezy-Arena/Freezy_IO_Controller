#include "web_manager.h"
#include "web_assets.h"
#include <esp_system.h>

// All OTA state is owned by the AsyncTCP callback task. Only the reboot flag
// crosses to loop(), publishing its deadline with atomic release/acquire.
void WebManager::_setupOtaRoutes() {
    char token[33];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
    _otaToken = token;
    _server.on("/update", HTTP_GET, [this](AsyncWebServerRequest* req) {
        String page = WebAssets::render("update", {{"updateToken", _otaToken}});
        auto* response = req->beginResponse(200, "text/html", page);
        response->addHeader("Cache-Control", "no-store");
        response->addHeader("X-Frame-Options", "DENY");
        req->send(response);
    });

    _server.on("/update", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            if (req->getResponse()) return; // Preserve an earlier 403/409 rejection.
            if (req != _otaRequest) {
                req->send(400, "text/plain", "No accepted firmware upload. Reload the update page.");
                return;
            }
            const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
            if (_otaError.isEmpty() && _otaReceived != req->contentLength())
                _otaError = "Incomplete firmware upload.";
            if (_otaError.isEmpty()) {
                esp_err_t result = esp_ota_end(_otaHandle);
                _otaWriting = false; // end releases the handle even on validation failure.
                if (result == ESP_OK && next) result = esp_ota_set_boot_partition(next);
                else if (result == ESP_OK) result = ESP_ERR_NOT_FOUND;
                if (result != ESP_OK) _otaError = String("Firmware validation/activation failed: ") + esp_err_to_name(result);
            }
            if (_otaWriting) { esp_ota_abort(_otaHandle); _otaWriting = false; }
            _otaRequest = nullptr;
            if (!_otaError.isEmpty()) {
                req->send(400, "text/plain", _otaError + " Current firmware remains selected.");
                return;
            }
            req->send(200, "text/plain", "Firmware installed. Rebooting now; wait about 15 seconds, then return to Device Configuration.");
            _rebootAt = millis() + 3000;
            _rebootPending = true;
        }, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                if (!req->hasHeader("X-Update-Token") || req->getHeader("X-Update-Token")->value() != _otaToken) {
                    req->send(403, "text/plain", "Reload the firmware update page before uploading."); return;
                }
                if (_otaRequest || _rebootPending) {
                    req->send(409, "text/plain", "Another update or reboot is in progress."); return;
                }
                _otaRequest = req; _otaReceived = 0; _otaError = "";
                req->client()->setRxTimeout(30);
                req->onDisconnect([this, req]() {
                    if (_otaRequest != req) return;
                    if (_otaWriting) esp_ota_abort(_otaHandle);
                    _otaWriting = false; _otaRequest = nullptr;
                });
                const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
                if (req->contentType() != "application/octet-stream" || !total || total != req->contentLength())
                    _otaError = "Expected an application binary with a known size.";
                else if (!next || next == esp_ota_get_running_partition())
                    _otaError = "No inactive OTA slot. Install the OTA partition layout over USB first.";
                else if (total > next->size) _otaError = "Firmware is too large for the OTA slot.";
                else {
                    // Erase sectors as data arrives rather than blocking to erase the whole slot.
                    esp_err_t result = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &_otaHandle);
                    if (result != ESP_OK) _otaError = String("Cannot start update: ") + esp_err_to_name(result);
                    else _otaWriting = true;
                }
            }
            if (req != _otaRequest || !_otaError.isEmpty()) return;
            if (index != _otaReceived || len > total - _otaReceived) {
                _otaError = "Invalid upload sequence."; return;
            }
            esp_err_t result = esp_ota_write(_otaHandle, data, len);
            if (result != ESP_OK) {
                _otaError = String("Firmware write failed: ") + esp_err_to_name(result);
                esp_ota_abort(_otaHandle); _otaWriting = false;
                return;
            }
            _otaReceived += len;
        });
}
