/*
 * PioTGuard-Sensors
 * David Ruiz Luque
 * Universidad de Malaga
 * daavidruiz01@outlook.com
 * 2024-06-05
 */

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

//MACROS CFG
#define NVS_ALARM       "alarm"
#define NVS_DOOR        "door"
#define NVS_GAS         "gas"
#define NVS_PRESENCE    "presence"


//Init NVS write mode
esp_err_t nvs_write_init(void);

//Write NVS
esp_err_t nvs_write(void);
#endif