#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "defines.h"

static const char *TAG_MQTT = "MQTT";
static const char *TAG_WIFI = "Wifi";

int mqtt_connected = 0;

//================================================================
//==========================SENSORS===============================
//================================================================

void sensorTask(void *pvParameters){

}

//================================================================
//=============================MQTT===============================
//================================================================
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    //Event management MQTT
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("MQTT conectado al broker\n");
            mqtt_connected = 1;
            break;
        case MQTT_EVENT_DISCONNECTED:
            printf("MQTT desconectado del broker\n");
            mqtt_connected = 0;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            printf("MQTT suscrito a un tema\n");
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            printf("MQTT canceló la suscripción a un tema\n");
            break;
        case MQTT_EVENT_PUBLISHED:
            printf("MQTT mensaje publicado correctamente\n");
            break;
        case MQTT_EVENT_DATA:
            printf("MQTT mensaje recibido:\n");
            printf("Tema: %.*s\n", event->topic_len, event->topic);
            printf("Datos: %.*s\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            printf("ERROR EVENT %d\n",event->error_handle->error_type);
            break;
        default:
            printf("Evento MQTT no manejado\n");
            break;
    }
}


void mqttTask(void *pvParameters) {
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
        .broker.address.port = MQTT_PORT,
        //.broker.address.transport = MQTT_TRANSPORT_OVER_TCP, 
        .broker.address.path = NULL,
        .broker.address.hostname = NULL
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_start(client);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);

    if(client == NULL){
        ESP_LOGE(TAG_MQTT, "Failed to initialize MQTT client");
        return;
    }
    
    // Esperar a que el cliente MQTT se conecte correctamente
    while (!mqtt_connected) {
        vTaskDelay(pdMS_TO_TICKS(200)); // Esperar 200 milisegundos
    }
    
    esp_mqtt_client_subscribe(client, "/server",  0);
    while (1){
        esp_mqtt_client_publish(client, "/sensor/info", "Hello from ESP32, MQTT!",  0,  0,  0);
        vTaskDelay(200);
    }
    
}

//================================================================
//=============================WIFI===============================
//================================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_WIFI, "Conectando a la red WiFi...");
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
        ESP_LOGI(TAG_WIFI, "Conectado a la red WiFi");
        ESP_LOGI(TAG_WIFI, "SSID: %s", event->ssid);
        ESP_LOGI(TAG_WIFI, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->bssid[0], event->bssid[1], event->bssid[2],
                 event->bssid[3], event->bssid[4], event->bssid[5]);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG_WIFI, "Desconectado de la red WiFi");
        esp_wifi_connect();
    }
}


void app_main(){
    esp_log_level_set("*", ESP_LOG_DEBUG);
    //================================================================
    //=======================WIFI INITIALITATION======================
    //================================================================

    // Inicialización del sistema de almacenamiento no volátil (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicialización del stack de TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());

    // Inicialización del controlador WiFi
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configuración de eventos WiFi
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // Configuración y conexión a la red WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

//TODO MEMORIA COMPARTIDA
    //MQTT Task en el Core0
    xTaskCreatePinnedToCore(&mqttTask, "mqtt_task", 4096, NULL, 5, NULL, 0);

    //Sensor Task
    xTaskCreatePinnedToCore(&sensorTask, "sensorTask",4096, NULL, 5, NULL, 1); 
}