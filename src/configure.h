// comment / un-comment the correct sensor type being used
#define SIMULATE_DHT_TYPE
//#define DHT11_TYPE
//#define DHT22_TYPE


#define TELEMETRY_SEND_INTERVAL 5000  // telemetry data sent every 5 seconds
#define PROPERTY_SEND_INTERVAL  15000 // property data sent every 15 seconds
#define SENSOR_READ_INTERVAL  2500    // read sensors every 2.5 seconds


// for DHT11/22, 
//   VCC: 5V or 3V
//   GND: GND
//   DATA: 2
int pinDHT = 2;

// MQTT publish topics
static const char PROGMEM IOT_EVENT_TOPIC[] = "devices/{device_id}/messages/events/";
static const char PROGMEM IOT_TWIN_REPORTED_PROPERTY[] = "$iothub/twin/PATCH/properties/reported/?$rid={request_id}";
static const char PROGMEM IOT_TWIN_REQUEST_TWIN_TOPIC[] = "$iothub/twin/GET/?$rid={request_id}";
static const char PROGMEM IOT_DIRECT_METHOD_RESPONSE_TOPIC[] = "$iothub/methods/res/{status}/?$rid={request_id}";

// MQTT subscribe topics
static const char PROGMEM IOT_TWIN_RESULT_TOPIC[] = "$iothub/twin/res/#";
static const char PROGMEM IOT_TWIN_DESIRED_PATCH_TOPIC[] = "$iothub/twin/PATCH/properties/desired/#";
static const char PROGMEM IOT_C2D_TOPIC[] = "devices/{device_id}/messages/devicebound/#";
static const char PROGMEM IOT_DIRECT_MESSAGE_TOPIC[] = "$iothub/methods/POST/#";
