//=====================WIFI DEFINES============================
#define WIFI_SSID   "rzzzJOQ24G"
#define WIFI_PASS   "cocinalimpia"

//=====================MQTT DEFINES============================
#define MQTT_BROKER     "mqtt://192.168.1.211" //"mqtt://rzzz.ddns.net"
#define MQTT_PORT       1883
#define MQTT_USERNAME   "PioTGuardSensors"
#define MQTT_PASSWORD   "***"
    //TOPICS
#define GAS_TOPIC_VALUE         "/sensor/gas/value"
#define DOOR_TOPIC_VALUE        "/sensor/door/value"
#define PRESENCE_TOPIC_VALUE    "/sensor/presence/value"


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