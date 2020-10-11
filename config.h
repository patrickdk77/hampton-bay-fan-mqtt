// Configure wifi settings
#define WIFI_SSID "ssid"
#define WIFI_PASS "pass"

// Configure MQTT broker settings
#define MQTT_HOST "192.168.1.1"
#define MQTT_PORT 1883
#define MQTT_USER "USER"
#define MQTT_PASS "PASS"
#define MQTT_CLIENT_NAME "rf-fans"   

// Compile in Hampton Bay FAN-9T (303.631mhz)
#define HAMPTONBAY

// Compile in Hampton Bay (dawnsun) A25-TX028 (304.015mhz approx)
#define HAMPTONBAY2

// Compile in Fanimation Slinger (303.870mhz approx)
#define FANIMATION


#define HAMPTONBAY_BASE_TOPIC "hamptonbay"
#define HAMPTONBAY2_BASE_TOPIC "hamptonbay2"
#define FANIMATION_BASE_TOPIC "fanimation"
