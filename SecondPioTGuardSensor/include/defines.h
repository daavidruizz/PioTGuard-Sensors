//=====================WIFI DEFINES============================
#define WIFI_SSID   "iPhone de David"
#define WIFI_PASS   "cocinalimpia"

//=====================MQTT DEFINES============================
#define MQTT_BROKER     "mqtts://rzzz.ddns.net"  //"mqtts://rzzz.ddns.net"
#define MQTT_PORT       8883
#define MQTT_USERNAME   "sensor"
#define MQTT_PASSWORD   "140516"
#define SERVER_CN       "rzzz.ddns.net"
    //TOPICS
#define GAS_TOPIC_VALUE         "/info/sensor/gas"
#define DOOR_TOPIC_VALUE        "/info/sensor/door"
#define PRESENCE_TOPIC_VALUE    "/info/sensor/presence"

#define DEVICE_INFO             "/info/device"
#define ALARM_TRIGGER           "/info/alarm"

#define CFG_ALARM               "/config/alarm"
#define CFG_DOOR                "/config/sensor/door"
#define CFG_GAS                 "/config/sensor/gas"
#define CFG_PRESENCE            "/config/sensor/presence"
#define CFG_ALL                 "/config/sensor/all"
#define REBOOT                  "/config/device/reboot"

#define DEVICE_SENSORS          "/info/device/sensors"
#define REQ_INFO                "/info/device/req"

//=======================SENSORS===============================
#define DOOR_ID             0
#define GAS_ID              1
#define PRESENCE_ID         2
#define DOOR_SENSOR         "Door Sensor"
#define PRESENCE_SENSOR     "Presence Sensor"
#define GAS_SENSOR          "Gas Sensor"


//========================PINOUT===============================
#define GAS_SENSOR_PIN      ADC1_CHANNEL_0
#define DOOR_SERNSOR_PIN    GPIO_NUM_4
#define PRESENCE_SENSOR_PIN GPIO_NUM_13

//========================DEBUG================================
#define MBEDTLS_DEBUG_LEVEL 3