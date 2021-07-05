/*
// Read temperature and humidity data from an Arduino MKR1000 or MKR1010 device using a DHT11/DHT22 sensor.
// The data is then sent to Azure IoT Central for visualizing via MQTT
//
// See the readme.md for details on connecting the sensor and setting up Azure IoT Central to recieve the data.
*/

const short VERSION = 1;

const char* PATH = "/update-v%d.bin";       // Set the URI to the .bin firmware
const unsigned long CHECK_INTERVAL = 6000;  // Time interval between update checks (ms)



#include <stdarg.h>
#include <time.h>
#include <SPI.h>

#include <FastLED.h>
// How many leds in your strip?
#define NUM_LEDS 16
#define DATA_PIN 4
// Define the array of leds
CRGB leds[NUM_LEDS];
uint8_t gHue = 0; // rotating "base color" used by many of the patterns

#include "secrets.h" 

#include "network.h"
//Must be defined after network.h
//#include "OTA/ArduinoOTA.h"
#include <SparkFun_ATECCX08a_Arduino_Library.h>



#include <SimpleDHT.h>
#ifdef ARDUINO_SAMD_NANO_33_IOT //change for samd generic define
  #include <RTCZero.h>
  #include <avr/dtostrf.h>
#else
    #include <stdlib_noniso.h>
#endif

#ifdef ARDUINO_SAMD_NANO_33_IOT
#include <Arduino_LSM6DS3.h>
#endif
/*  You need to go into this file and change this line from:
      #define MQTT_MAX_PACKET_SIZE 128
    to:
      #define MQTT_MAX_PACKET_SIZE 2048
*/
#include <PubSubClient.h>

// change the values for Wi-Fi, Azure IoT Central device, and DHT sensor in this file
#include "./configure.h"

// this is an easy to use NTP Arduino library by Stefan Staub - updates can be found here https://github.com/sstaub/NTP
#include "./ntp.h"
#include "./sha256.h"
#include "./base64.h"
#include "./parson.h"

#include "./utils.h"

int getHubHostName(char *scopeId, char* deviceId, char* key, char *hostName);

enum dht_type {simulated, dht22, dht11}; 

#if defined DHT22_TYPE
SimpleDHT22 dhtSensor(pinDHT);
dht_type dhtType = dht22;
#elif defined DHT11_TYPE
SimpleDHT11 dhtSensor(pinDHT);
dht_type dhtType = dht11;
#else
dht_type dhtType = simulated;
#endif

String iothubHost;
String deviceId;
String sharedAccessKey;

PubSubClient *mqtt_client = NULL;

bool timeSet = false;
bool wifiConnected = false;
bool mqttConnected = false;

time_t this_second = 0;
time_t checkTime = 1300000000;

long lastTelemetryMillis = 0;
long lastPropertyMillis = 0;
long lastSensorReadMillis = 0;

float tempValue = 0.0;
float humidityValue = 0.0;
float x, y, z;
int dieNumberValue = 1;


int requestId = 0;
int twinRequestId = -1;
NetworkClient adapter;

// create an NTP object
NTP ntp(adapter.getUdpClient());
#ifdef SAMD_SERIES
  // Create an rtc object
  RTCZero rtc;
#endif

ATECCX08A atecc;

#include "./iotc_dps.h"

// get the time from NTP and set the real-time clock on the MKR10x0
void getTime() {
    Serial.println(F("Getting the time from time service: "));

    ntp.begin();
    ntp.update();
    Serial.print(F("Current time: "));
    Serial.print(ntp.formattedTime("%d. %B %Y - "));
    Serial.println(ntp.formattedTime("%A %T"));
#ifdef SAMD_SERIES
    rtc.begin();
    rtc.setEpoch(ntp.epoch());
#endif
    timeSet = true;
}

void acknowledgeSetting(const char* propertyKey, const char* propertyValue, int version) {
        // for IoT Central need to return acknowledgement
        const static char* PROGMEM responseTemplate = "{\"%s\":{\"value\":%s,\"statusCode\":%d,\"status\":\"%s\",\"desiredVersion\":%d}}";
        char payload[1024];
        sprintf(payload, responseTemplate, propertyKey, propertyValue, 200, F("completed"), version);
        Serial_printf("Sending acknowledgement: %s\n\n", payload);
        String topic = (String)IOT_TWIN_REPORTED_PROPERTY;
        char buff[20];
        topic.replace(F("{request_id}"), itoa(requestId, buff, 10));
        mqtt_client->publish(topic.c_str(), payload);
        requestId++;
}

void handleDirectMethod(String topicStr, String payloadStr) {
    String msgId = topicStr.substring(topicStr.indexOf("$RID=") + 5);
    String methodName = topicStr.substring(topicStr.indexOf(F("$IOTHUB/METHODS/POST/")) + 21, topicStr.indexOf("/?$"));
    Serial_printf((char*)F("Direct method call:\n\tMethod Name: %s\n\tParameters: %s\n"), methodName.c_str(), payloadStr.c_str());
    if (strcmp(methodName.c_str(), "ECHO") == 0) {
       Serial.println("Echo method");
        // acknowledge receipt of the command
        String response_topic = (String)IOT_DIRECT_METHOD_RESPONSE_TOPIC;
        char buff[20];
        response_topic.replace(F("{request_id}"), msgId);
        response_topic.replace(F("{status}"), F("200"));  //OK
        mqtt_client->publish(response_topic.c_str(), "");

        // output the message as morse code
        JSON_Value *root_value = json_parse_string(payloadStr.c_str());
        JSON_Object *root_obj = json_value_get_object(root_value);
        const char* msg = json_object_get_string(root_obj, "displayedValue");
        if (strcmp(payloadStr.c_str(), "\"ON\"") == 0){
                rainbow();
  FastLED.show();
        }
           if (strcmp(payloadStr.c_str(), "\"OFF\"") == 0){
               allBlack();
  FastLED.show();
        }
        //morse_encodeAndFlash(msg);
        json_value_free(root_value);
    }
 
    if (strcmp(methodName.c_str(), "UPDATE") == 0){
      //handleSketchDownload();
    }
}
void rainbow(){
  // FastLED's built-in rainbow generator
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
   
}

void allBlack(){
  for (int i = 0; i < 16; i++){
    leds[i] = CRGB::Black;
  }
   
}

void handleCloud2DeviceMessage(String topicStr, String payloadStr) {
    Serial_printf((char*)F("Cloud to device call:\n\tPayload: %s\n"), payloadStr.c_str());
}

void handleTwinPropertyChange(String topicStr, String payloadStr) {
    // read the property values sent using JSON parser
    JSON_Value *root_value = json_parse_string(payloadStr.c_str());
    JSON_Object *root_obj = json_value_get_object(root_value);
    const char* propertyKey = json_object_get_name(root_obj, 0);
    double propertyValueNum;
    double propertyValueBool;
    double version;
    if (strcmp(propertyKey, "fanSpeed") == 0 || strcmp(propertyKey, "setVoltage") == 0 || strcmp(propertyKey, "setCurrent") == 0 || strcmp(propertyKey, "activateIR") == 0) {
        JSON_Object* valObj = json_object_get_object(root_obj, propertyKey);
        if (strcmp(propertyKey, "activateIR") == 0) {
            propertyValueBool = json_object_get_boolean(valObj, "value");
        } else {
            propertyValueNum = json_object_get_number(valObj, "value");
        }
        version = json_object_get_number(root_obj, "$version");
        char propertyValueStr[8];
        if (strcmp(propertyKey, "activateIR") == 0) {
            if (propertyValueBool) {
                strcpy(propertyValueStr, "true");
            } else {
                strcpy(propertyValueStr, "false");
            }
        } else {
            itoa(propertyValueNum, propertyValueStr, 10);
        }
        Serial_printf("\n%s setting change received with value: %s\n", propertyKey, propertyValueStr);
        acknowledgeSetting(propertyKey, propertyValueStr, version);
    }
    json_value_free(root_value);
}

// callback for MQTT subscriptions
void callback(char* topic, byte* payload, unsigned int length) {
    String topicStr = (String)topic;
    topicStr.toUpperCase();
    String payloadStr = (String)((char*)payload);
    payloadStr.remove(length);

    if (topicStr.startsWith(F("$IOTHUB/METHODS/POST/"))) { // direct method callback
        handleDirectMethod(topicStr, payloadStr);
    } else if (topicStr.indexOf(F("/MESSAGES/DEVICEBOUND/")) > -1) { // cloud to device message
        handleCloud2DeviceMessage(topicStr, payloadStr);
    } else if (topicStr.startsWith(F("$IOTHUB/TWIN/PATCH/PROPERTIES/DESIRED"))) {  // digital twin desired property change
        handleTwinPropertyChange(topicStr, payloadStr);
    } else if (topicStr.startsWith(F("$IOTHUB/TWIN/RES"))) { // digital twin response
        int result = atoi(topicStr.substring(topicStr.indexOf(F("/RES/")) + 5, topicStr.indexOf(F("/?$"))).c_str());
        int msgId = atoi(topicStr.substring(topicStr.indexOf(F("$RID=")) + 5, topicStr.indexOf(F("$VERSION=")) - 1).c_str());
        if (msgId == twinRequestId) {
            // twin request processing
            twinRequestId = -1;
            // output limited to 128 bytes so this output may be truncated
            Serial_printf((char*)F("Current state of device twin:\n\t%s"), payloadStr.c_str());
            Serial.println();
        } else {
            if (result >= 200 && result < 300) {
                Serial_printf((char*)F("--> IoT Hub acknowledges successful receipt of twin property: %d\n"), msgId);
            } else {
                Serial_printf((char*)F("--> IoT Hub could not process twin property: %d, error: %d\n"), msgId, result);
            }
        }
    } else { // unknown message
        Serial_printf((char*)F("Unknown message arrived [%s]\nPayload contains: %s"), topic, payloadStr.c_str());
    }
}

// connect to Azure IoT Hub via MQTT
void connectMQTT(String deviceId, String username, String password) {
    mqtt_client->disconnect();

    Serial.println(F("Starting IoT Hub connection"));
    int retry = 0;
    while(retry < 10 && !mqtt_client->connected()) {     
        if (mqtt_client->connect(deviceId.c_str(), username.c_str(), password.c_str())) {
                Serial.println(F("===> mqtt connected"));
                mqttConnected = true;
        } else {
            Serial.print(F("---> mqtt failed, rc="));
            Serial.println(mqtt_client->state());
            delay(2000);
            retry++;
        }
    }
}

// create an IoT Hub SAS token for authentication
String createIotHubSASToken(char *key, String url, long expire){
    url.toLowerCase();
    String stringToSign = url + "\n" + String(expire);
    int keyLength = strlen(key);

    int decodedKeyLength = base64_dec_len(key, keyLength);
    char decodedKey[decodedKeyLength];

    base64_decode(decodedKey, key, keyLength);

    Sha256 *sha256 = new Sha256();
    sha256->initHmac((const uint8_t*)decodedKey, (size_t)decodedKeyLength);
    sha256->print(stringToSign);
    char* sign = (char*) sha256->resultHmac();
    int encodedSignLen = base64_enc_len(HASH_LENGTH);
    char encodedSign[encodedSignLen];
    base64_encode(encodedSign, sign, HASH_LENGTH);
    delete(sha256);

    return (char*)F("SharedAccessSignature sr=") + url + (char*)F("&sig=") + urlEncode((const char*)encodedSign) + (char*)F("&se=") + String(expire);
}

// reads the value from the DHT sensor if present else generates a random value
void readSensors() {
    dieNumberValue = random(1, 7);

#if defined DHT11_TYPE || defined DHT22_TYPE
    int err = SimpleDHTErrSuccess;
    if ((err = dhtSensor.read2(&tempValue, &humidityValue, NULL)) != SimpleDHTErrSuccess) {
        Serial_printf("Read DHT sensor failed (Error:%d)", err); 
        tempValue = -999.99;
        humidityValue = -999.99;
    }
#else
    tempValue = random(0, 7500) / 100.0;
    humidityValue = random(0, 9999) / 100.0;
#endif
#ifdef ARDUINO_SAMD_NANO_33_IOT
    if (IMU.accelerationAvailable()) {
          IMU.readAcceleration(x, y, z);
  
          Serial.print(x);
          Serial.print('\t');
          Serial.print(y);
          Serial.print('\t');
          Serial.println(z);
    }
#endif
}

//void handleSketchDownload() {
// 
//
//  // Time interval check
//  static unsigned long previousMillis;
//  unsigned long currentMillis = millis();
//  if (currentMillis - previousMillis < CHECK_INTERVAL)
//    return;
//  previousMillis = currentMillis;
//
//  char buff[32];
//  snprintf(buff, sizeof(buff), PATH, VERSION + 1);
//
//  Serial.print("Check for update file ");
//  Serial.println(buff);
//  HttpClient client = adapter.getHttpClient();
//  // Make the GET request
//  client.get(buff);
//
//  int statusCode = client.responseStatusCode();
//  Serial.print("Update status code: ");
//  Serial.println(statusCode);
//  if (statusCode != 200) {
//    client.stop();
//    return;
//  }
//
//  long length = client.contentLength();
//  if (length == HttpClient::kNoContentLengthHeader) {
//    client.stop();
//    Serial.println("Server didn't provide Content-length header. Can't continue with update.");
//    return;
//  }
//  Serial.print("Server returned update file of size ");
//  Serial.print(length);
//  Serial.println(" bytes");
//
//  if (!InternalStorage.open(length)) {
//    client.stop();
//    Serial.println("There is not enough space to store the update. Can't continue with update.");
//    return;
//  }
//  byte b;
//  while (length > 0) {
//    if (!client.readBytes(&b, 1)) // reading a byte with timeout
//      break;
//    InternalStorage.write(b);
//    length--;
//  }
//  InternalStorage.close();
//  client.stop();
//  if (length > 0) {
//    Serial.print("Timeout downloading update file at ");
//    Serial.print(length);
//    Serial.println(" bytes. Can't continue with update.");
//    return;
//  }
//
//  Serial.println("Sketch update apply and reset.");
//  Serial.flush();
//  InternalStorage.apply(); // this doesn't return
//}

void setup() {
    Serial.begin(115200);

    // uncomment this line to add a small delay to allow time for connecting serial moitor to get full debug output
    delay(5000); 
 
    Serial_printf((char*)F("Hello, starting up the %s device\n"), DEVICE_NAME);

    // seed pseudo-random number generator for die roll and simulated sensor values
    randomSeed(millis());
#ifdef ARDUINO_SAMD_NANO_33_IOT
    if (!IMU.begin()){
      Serial.println("Failed to initialize IMU!");
    }
#endif

    if (atecc.begin() == true)
    {
      Serial.println("Successful wakeUp(). I2C connections are good.");
    }

    // check for configuration
    if (!(atecc.configLockStatus && atecc.dataOTPLockStatus && atecc.slot0LockStatus))
    {
      Serial.print("Device not configured. Please configure the ATECC608.");
    }

    adapter.connectToWifi();

    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed

    leds[0] = CRGB::Black;
    FastLED.show();

     // start the WiFi OTA library with internal (flash) based storage
    //ArduinoOTA.begin(adapter.localIP(), "Arduino", "password", InternalStorage);
    
    // get current UTC time
    getTime();

    Serial.println("Getting IoT Hub host from Azure IoT DPS");
    deviceId = iotc_deviceId;
    sharedAccessKey = iotc_deviceKey;
    char hostName[64] = {0};
    getHubHostName((char*)iotc_scopeId, (char*)iotc_deviceId, (char*)iotc_deviceKey, hostName);
    iothubHost = hostName;

    // create SAS token and user name for connecting to MQTT broker
    String url = iothubHost + urlEncode(String((char*)F("/devices/") + deviceId).c_str());
    char *devKey = (char *)sharedAccessKey.c_str();
#ifdef SAMD_SERIES
    long expire = rtc.getEpoch() + 864000;
#else
    ntp.update();
    long expire = ntp.epoch() + 864000;
#endif

    String sasToken = createIotHubSASToken(devKey, url, expire);
    String username = iothubHost + "/" + deviceId + (char*)F("/api-version=2016-11-14");

    // connect to the IoT Hub MQTT broker
    adapter.getNetworkClient().connect(iothubHost.c_str(), 8883);
    mqtt_client = new PubSubClient(iothubHost.c_str(), 8883, adapter.getNetworkClient());
    connectMQTT(deviceId, username, sasToken);
    mqtt_client->setCallback(callback);

    // add subscriptions
    mqtt_client->subscribe(IOT_TWIN_RESULT_TOPIC);  // twin results
    mqtt_client->subscribe(IOT_TWIN_DESIRED_PATCH_TOPIC);  // twin desired properties
    String c2dMessageTopic = IOT_C2D_TOPIC;
    c2dMessageTopic.replace(F("{device_id}"), deviceId);
    mqtt_client->subscribe(c2dMessageTopic.c_str());  // cloud to device messages
    mqtt_client->subscribe(IOT_DIRECT_MESSAGE_TOPIC); // direct messages

    // request full digital twin update
    String topic = (String)IOT_TWIN_REQUEST_TWIN_TOPIC;
    char buff[20];
    topic.replace(F("{request_id}"), itoa(requestId, buff, 10));
    twinRequestId = requestId;
    requestId++;
    mqtt_client->publish(topic.c_str(), "");

    // initialize timers
    lastTelemetryMillis = millis();
    lastPropertyMillis = millis();
}

// main processing loop
void loop() {

    // check for WiFi OTA updates
    //ArduinoOTA.poll();
  
    // give the MQTT handler time to do it's thing
    mqtt_client->loop();

    // read the sensor values
    if (millis() - lastSensorReadMillis > SENSOR_READ_INTERVAL) {
        readSensors();
        lastSensorReadMillis = millis();
    }
    
    // send telemetry values every 5 seconds
    if (mqtt_client->connected() && millis() - lastTelemetryMillis > TELEMETRY_SEND_INTERVAL) {
        Serial.println(F("Sending telemetry ..."));
        String topic = (String)IOT_EVENT_TOPIC;
        topic.replace(F("{device_id}"), deviceId);
        char buff[10];
        String payload = F("{\"temp\": {temp}, \"humidity\": {humidity}, \"acc\":{\"x\":{accx},\"y\": {accy}, \"z\": {accz}}}");
        payload.replace(F("{temp}"), dtostrf(tempValue, 7, 2, buff));
        payload.replace(F("{humidity}"), dtostrf(humidityValue, 7, 2, buff));
        payload.replace(F("{accx}"), dtostrf(x, 7, 2, buff));
        payload.replace(F("{accy}"), dtostrf(y, 7, 2, buff));
        payload.replace(F("{accz}"), dtostrf(z, 7, 2, buff));
        Serial_printf("\t%s\n", payload.c_str());
        mqtt_client->publish(topic.c_str(), payload.c_str());

        lastTelemetryMillis = millis();
    }

    // send a property update every 15 seconds
    if (mqtt_client->connected() && millis() - lastPropertyMillis > PROPERTY_SEND_INTERVAL) {
        Serial.println(F("Sending digital twin property ..."));

        String topic = (String)IOT_TWIN_REPORTED_PROPERTY;
        char buff[20];
        topic.replace(F("{request_id}"), itoa(requestId, buff, 10));
        String payload = F("{\"dieNumber\": {dieNumberValue}}");
        payload.replace(F("{dieNumberValue}"), itoa(dieNumberValue, buff, 10));

        mqtt_client->publish(topic.c_str(), payload.c_str());
        requestId++;

        lastPropertyMillis = millis();
    }
}

void printInfo()
{
  // Read all 128 bytes of Configuration Zone
  // These will be stored in an array within the instance named: atecc.configZone[128]
  atecc.readConfigZone(false); // Debug argument false (OFF)

  // Print useful information from configuration zone data
  Serial.println();

  Serial.print("Serial Number: \t");
  for (int i = 0 ; i < 9 ; i++)
  {
    if ((atecc.serialNumber[i] >> 4) == 0) Serial.print("0"); // print preceeding high nibble if it's zero
    Serial.print(atecc.serialNumber[i], HEX);
  }
  Serial.println();

  Serial.print("Rev Number: \t");
  for (int i = 0 ; i < 4 ; i++)
  {
    if ((atecc.revisionNumber[i] >> 4) == 0) Serial.print("0"); // print preceeding high nibble if it's zero
    Serial.print(atecc.revisionNumber[i], HEX);
  }
  Serial.println();

  Serial.print("Config Zone: \t");
  if (atecc.configLockStatus) Serial.println("Locked");
  else Serial.println("NOT Locked");

  Serial.print("Data/OTP Zone: \t");
  if (atecc.dataOTPLockStatus) Serial.println("Locked");
  else Serial.println("NOT Locked");

  Serial.print("Data Slot 0: \t");
  if (atecc.slot0LockStatus) Serial.println("Locked");
  else Serial.println("NOT Locked");

  Serial.println();

  // omitted printing public key, to keep this example simple and focused on just random numbers.
}
