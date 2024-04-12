#ifndef NVS_WRITE_H
#define NVS_WRITE_H
#include "nvs.h"

//KEYS MACROS NVS
#define SSID        "SSID"
#define SSID_PASS   "SSID_PASS"
#define CN          "CN"
#define KEY_PASS    "KEY_PASS"
#define MQTT_USER   "MQTT_USER"
#define MQTT_PASS   "MQTT_PASS"

//Init NVS write mode
esp_err_t nvs_write_init(void);

//Write NVS
esp_err_t nvs_write(void);
#endif