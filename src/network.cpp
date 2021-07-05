#include "network.h"
#include "secrets.h" 
#include <ArduinoHttpClient.h>


const char* SERVER = "www.my-hostname.it";  // Set your correct hostname
const unsigned short SERVER_PORT = 443;     // Commonly 80 (HTTP) | 443 (HTTPS)


// create a WiFi UDP object for NTP to use
WiFiUDP wifiUdp;


WiFiClient wifiClient;

NetworkClient::NetworkClient(){

    
}

Client& NetworkClient::getNetworkClient(){
  return wifiClient;
}

HttpClient& NetworkClient::getHttpClient(){
  // HttpClient client(wifiClient, SERVER, SERVER_PORT);  // 
  HttpClient client = HttpClient(wifiClient, SERVER, SERVER_PORT);  // HTTPS
  return client;
}

UDP& NetworkClient::getUdpClient(){
    return wifiUdp;
}

IPAddress NetworkClient::localIP() const
{
  return WiFi.localIP();
}


void NetworkClient::connectToWifi(){
      // attempt to connect to Wifi network:
    //Serial.print((char*)F("WiFi Firmware version is "));
    #ifndef ESP32-POE
      //Serial.println(WiFi.firmwareVersion());
      int status = WL_IDLE_STATUS;
      while ( status != WL_CONNECTED) {
          //Serial_printf((char*)F("Attempting to connect to Wi-Fi SSID: %s \n"), wifi_ssid);
          // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
          status = WiFi.begin(wifi_ssid, wifi_password);
          if (status == WL_CONNECTED) {
            //Serial_printf((char*)F("Connected to Wi-Fi SSID: %s \n"), wifi_ssid);
          }
          delay(1000);
      }
    #endif
}

