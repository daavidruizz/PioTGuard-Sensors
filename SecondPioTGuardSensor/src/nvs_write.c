/*
 * PioTGuard-Sensors
 * David Ruiz Luque
 * Universidad de Malaga
 * daavidruiz01@outlook.com
 * 2024-06-05
 */

#include "nvs_write.h"
#include "nvs_flash.h"
#include "credentials.h"
#include "defines.h"

esp_err_t nvs_write_init(void){
    esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
    return ESP_OK;
}

esp_err_t nvs_write(void){
    //Abrimos NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    
    if (err != ESP_OK) return err;

    //Escribimos con key, value
    err = nvs_set_str(nvs_handle, SSID, WIFI_SSID);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, SSID_PASS, WIFI_PASS);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, CN, SERVER_CN);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, KEY_PASS, PARAPHRASE_KEY);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, MQTT_USER, MQTT_USERNAME);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_set_str(nvs_handle, MQTT_PASS, MQTT_PASSWORD);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    //Inicializamos valores de la cfg
    ESP_ERROR_CHECK(nvs_set_i8(nvs_handle, NVS_ALARM, 0));
    ESP_ERROR_CHECK(nvs_set_i8(nvs_handle, NVS_DOOR, 0));
    ESP_ERROR_CHECK(nvs_set_i8(nvs_handle, NVS_GAS, 0));
    ESP_ERROR_CHECK(nvs_set_i8(nvs_handle, NVS_PRESENCE, 0));

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}