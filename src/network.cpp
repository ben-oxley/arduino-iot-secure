#include "network.h"


// create a WiFi UDP object for NTP to use
WiFiUDP wifiUdp;

WiFiSSLClient wifiClient;

NetworkClient::NetworkClient(){

    
}

Client& NetworkClient::getNetworkClient(){
  return wifiClient;
}

UDP& NetworkClient::getUdpClient(){
    return wifiUdp;
}

IPAddress NetworkClient::localIP() const
{
  return WiFi.localIP();
}
