#pragma once
#include <Arduino.h>
#include <ETH.h>
#include <Network.h>
#include <Preferences.h>
#include <atomic>
#include "../role_config.h"

// W5500 SPI pins
#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_PHY_ADDR 1
#define ETH_PHY_CS 14
#define ETH_PHY_IRQ 10
#define ETH_PHY_RST 9
#define ETH_PHY_SPI_HOST SPI3_HOST
#define ETH_PHY_SPI_SCK 13
#define ETH_PHY_SPI_MISO 12
#define ETH_PHY_SPI_MOSI 11

// Network defaults — overridden by Preferences later
#define NET_DEFAULT_IP "192.168.1.200"
#define NET_DEFAULT_GW "192.168.1.1"
#define NET_DEFAULT_SUBNET "255.255.255.0"
#define NET_DEFAULT_DNS1 "8.8.8.8"
#define NET_DEFAULT_DNS2 "8.8.4.4"
#define NET_PREFS_NS "network" // Preferences namespace

class EthManager
{
public:
    void begin(bool stopController = false);
    void update(); // Call from loop()
    // Stops and starts the Ethernet driver again, which hard-resets the W5500
    // through ETH_PHY_RST. Used when receiving has stopped with the link up.
    void restart(const char* reason);

    bool isConnected();
    IPAddress localIP();
    String localIPString();

    // These will be driven by Preferences/webserver later
    bool useDHCP = true;
    String staticIP = NET_DEFAULT_IP;
    String staticGW = NET_DEFAULT_GW;
    LedControlMode  ledControlMode  = LED_CONTROL_COIL;

    // Preferences — stubbed and ready to expand
    void loadPreferences();
    void savePreferences();

private:
    static void _onEvent(arduino_event_id_t event, arduino_event_info_t info);
    static std::atomic<bool> _connected;
    bool _stopController = false;
    void _start();

    Preferences _prefs;
};
