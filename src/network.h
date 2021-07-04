#include <Udp.h>

// are we compiling against the Arduino MKR1000
#if defined(ARDUINO_SAMD_MKR1000) && !defined(WIFI_101)
  #include <WiFi101.h>
  #define DEVICE_NAME "Arduino MKR1000"
#endif

// are we compiling against the Arduino MKR1010
#ifdef ARDUINO_SAMD_MKRWIFI1010
  #include <WiFiNINA.h>
  #include <WiFiUdp.h>
  #define DEVICE_NAME "Arduino MKR1010"
#endif

#ifdef ARDUINO_SAMD_NANO_33_IOT
  #include <WiFiNINA.h>
  #include <WiFiUdp.h>
  #define DEVICE_NAME "Arduino Nano 33 IOT"
#endif

#ifdef ESP32-POE
  #include <ETH.h>
  #include "Wifi.h"
  #include "AsyncUDP.h"
  #define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT
  #define ETH_PHY_POWER 12
  #define DEVICE_NAME "ESP32 POE"
#endif
#include <ArduinoHttpClient.h>



class NetworkClient {

  public:
    NetworkClient();
    IPAddress localIP() const;
    Client& getNetworkClient();
    HttpClient& getHttpClient();
    UDP& getUdpClient();
    void connectToWifi();

};
