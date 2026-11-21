#include "network_manager.h"

std::atomic<bool> EthManager::_connected{false};

void EthManager::_onEvent(arduino_event_id_t event, arduino_event_info_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("[NET] Ethernet started");
        ETH.setHostname("esp32-s3");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("[NET] Link up");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        _connected = true;
        Serial.printf("[NET] IP: %s  Speed: %dMbps  Duplex: %s\n",
                      ETH.localIP().toString().c_str(),
                      ETH.linkSpeed(),
                      ETH.fullDuplex() ? "Full" : "Half");
        break;
    case ARDUINO_EVENT_ETH_LOST_IP:
        _connected = false;
        Serial.println("[NET] IP lost");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        _connected = false;
        Serial.println("[NET] Link down");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        _connected = false;
        Serial.println("[NET] Ethernet stopped");
        break;
    default:
        break;
    }
}

void EthManager::loadPreferences()
{
    _prefs.begin(NET_PREFS_NS, true); // read-only
    useDHCP = _prefs.getBool("useDHCP", true);
    staticIP = _prefs.getString("staticIP", NET_DEFAULT_IP);
    staticGW = _prefs.getString("staticGW", NET_DEFAULT_GW);
    ledControlMode  = (LedControlMode)_prefs.getUChar("ledControl", LED_CONTROL_COIL);
    _prefs.end();
    Serial.printf("[NET] Prefs loaded — DHCP: %s  IP: %s\n",
                  useDHCP ? "yes" : "no", staticIP.c_str());
}

void EthManager::savePreferences()
{
    _prefs.begin(NET_PREFS_NS, false); // read-write
    _prefs.putBool("useDHCP", useDHCP);
    _prefs.putString("staticIP", staticIP);
    _prefs.putString("staticGW", staticGW);
    _prefs.putUChar("ledControl",   (uint8_t)ledControlMode);
    _prefs.end();
    Serial.println("[NET] Prefs saved");
}

void EthManager::begin(bool stopController) {
    loadPreferences();
    _stopController = stopController;

    Network.onEvent(_onEvent);
    _start();
}

void EthManager::restart(const char* reason) {
    Serial.printf("[NET] Restarting Ethernet: %s\n", reason);
    _connected = false;
    ETH.end();
    _start();
}

void EthManager::_start() {
    bool stopController = _stopController;
    if (!ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR,
                   ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST,
                   stopController ? SPI2_HOST : ETH_PHY_SPI_HOST,
                   ETH_PHY_SPI_SCK, ETH_PHY_SPI_MISO, ETH_PHY_SPI_MOSI)) {
        Serial.println("[NET] Ethernet failed to start");
    }

    if (!useDHCP) {
        IPAddress localIP, localGW, subnet, dns1, dns2;
        if (localIP.fromString(staticIP) && localGW.fromString(staticGW)) {
            subnet.fromString(NET_DEFAULT_SUBNET);
            dns1.fromString(NET_DEFAULT_DNS1);
            dns2.fromString(NET_DEFAULT_DNS2);
            ETH.config(localIP, localGW, subnet, dns1, dns2);
            Serial.println("[NET] Static IP configured");
        } else {
            Serial.println("[NET] Invalid static IP — falling back to DHCP");
        }
    }

    // IP acquisition is asynchronous. Inputs and WebSockets must keep running
    // when the cable is absent at boot.

    // Full connection summary
    Serial.println("[NET] ================================");
    Serial.printf( "[NET]  IP Address : %s\n",  ETH.localIP().toString().c_str());
    Serial.printf( "[NET]  Gateway    : %s\n",  ETH.gatewayIP().toString().c_str());
    Serial.printf( "[NET]  Subnet     : %s\n",  ETH.subnetMask().toString().c_str());
    Serial.printf( "[NET]  DNS        : %s\n",  ETH.dnsIP().toString().c_str());
    Serial.printf( "[NET]  MAC        : %s\n",  ETH.macAddress().c_str());
    Serial.printf( "[NET]  Speed      : %dMbps %s\n", ETH.linkSpeed(),
                                                       ETH.fullDuplex() ? "Full" : "Half");
    Serial.printf( "[NET]  Mode       : %s\n",  useDHCP ? "DHCP" : "Static");
    Serial.println("[NET] ================================");
}

void EthManager::update()
{
    // Ready for websocket keepalive, reconnect logic etc.
}

bool EthManager::isConnected()
{
    return _connected;
}

IPAddress EthManager::localIP()
{
    return ETH.localIP();
}

String EthManager::localIPString()
{
    return ETH.localIP().toString();
}
