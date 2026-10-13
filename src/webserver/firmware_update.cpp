#include "web_manager.h"
#include "page_branding.h"
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
        String page = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update</title><style>
*{box-sizing:border-box}body{margin:0;padding:40px 16px;background:#0f1117;color:#e2e8f0;font:16px system-ui}
main{max-width:520px;margin:auto;padding:32px;background:#1a1d27;border:1px solid #2d3148;border-radius:12px}
h1{font-size:1.3rem}p{line-height:1.5}a{color:#c7d2fe}label,input,button,progress{display:block;width:100%;margin:16px 0}
button{padding:12px;background:#6366f1;color:white;border:0;border-radius:8px;font:inherit;cursor:pointer}
button:disabled{opacity:.5;cursor:wait}.warning{padding:12px;background:#2b2111;color:#fde68a;border-radius:8px}
#status{overflow-wrap:anywhere}progress{accent-color:#6366f1}</style></head><body><main>
)HTML" DEVICE_TITLE_BAR R"HTML(
<h1>Firmware Update</h1><p>Choose the <strong>firmware.bin</strong> built for this Freezy IO Controller ESP32-S3.</p>
<p class="warning">Maintenance only: take the arena out of service first. Updating can interrupt stop inputs and outputs, and restarts the controller. Keep power and Ethernet connected.</p>
<p>Use the application binary, not bootloader.bin, partitions.bin, or a merged flash image. Saved settings are retained.</p>
<form id="form"><label for="firmware">Firmware (.bin)</label><input id="firmware" type="file" accept=".bin" required>
<button id="upload" type="submit">Upload and Update</button></form>
<progress id="progress" max="100" value="0" aria-label="Upload progress"></progress>
<p id="status" role="status" aria-live="polite">Ready.</p><a href="/">Back to Device Configuration</a>
<script>
const token=')HTML" + _otaToken + R"HTML(';
const form=document.getElementById('form'),file=document.getElementById('firmware'),button=document.getElementById('upload'),
status=document.getElementById('status'),progress=document.getElementById('progress');
form.onsubmit=e=>{
 e.preventDefault(); const bin=file.files[0];
 if(!bin||!bin.size||!bin.name.toLowerCase().endsWith('.bin')){status.textContent='Select a nonempty .bin file.';return;}
 button.disabled=file.disabled=true; progress.value=0; status.textContent='Uploading firmware...';
 const xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.timeout=180000;
 xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-Update-Token',token);
 xhr.upload.onprogress=e=>{if(e.lengthComputable){progress.value=Math.round(e.loaded/e.total*100);status.textContent=progress.value===100?'Upload sent. Validating firmware...':'Uploading: '+progress.value+'%';}};
 const retry=message=>{status.textContent=message;button.disabled=file.disabled=false;};
 xhr.onload=()=>{if(xhr.status===200){status.textContent=xhr.responseText;progress.value=100;}else retry(xhr.responseText||'Update failed. Reload this page and retry.');};
 xhr.onerror=xhr.ontimeout=()=>retry('Connection lost or timed out. Update result is unknown. Wait for the controller, then reload this page before retrying.');
 xhr.send(bin);
};
</script></main></body></html>)HTML";
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
