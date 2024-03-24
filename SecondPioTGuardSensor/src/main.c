#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "defines.h"
#include "time.h"
#include "sys/time.h"
#include "driver/adc.h"
#include <lwip/apps/sntp.h>
#include <cJSON.h>
#include "esp_system.h" // Para esp_chip_info
#include "mbedtls/debug.h"


static const char *TAG_MQTT = "MQTT";
static const char *TAG_WIFI = "Wifi";
static const char *TAG_NTP = "NTP";

int mqtt_connected = 0;

//================================================================
//============================DEBUG===============================
//================================================================


//================================================================
//=======================CERTIFICATES=============================
//================================================================

extern const uint8_t MQTTca_crt_start[] asm("_binary_MQTTca_crt_start");
extern const uint8_t MQTTca_crt_end[]   asm("_binary_MQTTca_crt_end");
extern const uint8_t sensorclient_crt_start[] asm("_binary_sensorclient_crt_start");
extern const uint8_t sensorclient_crt_end[]   asm("_binary_sensorclient_crt_end");
extern const uint8_t sensorclient_key_start[] asm("_binary_sensorclient_key_start");
extern const uint8_t sensorclient_key_end[]   asm("_binary_sensorclient_key_end");

//================================================================
//======================SHARED MEMORY=============================
//================================================================
typedef struct {
    time_t time;
    char sensor[12];

} SharedMemory;

typedef struct {
    char dateTimeString[64];
    char sensor[16];
    int8_t sensorID;
    float value;

} SharedMemoryLOG;
SharedMemoryLOG *sharedLog = NULL;
SemaphoreHandle_t mutexSensorLog;
QueueHandle_t logQ;

QueueHandle_t dataQ;


//================================================================
//===================DATE TIME / JSON=============================
//================================================================

void getDateTimeString(char *dateTimeString) {
    time_t now;
    struct tm timeinfo;

    // Obtener el tiempo actual
    time(&now);
    localtime_r(&now, &timeinfo);

    // Formatear la fecha y hora como una cadena
    strftime(dateTimeString, 64, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

void DateTime(){
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init(); //Horario verano
    time_t t = 0;
    struct tm timeinfo = { 0 };
    
    int8_t max = 0;
    while (timeinfo.tm_year < (2016 - 1900)) { //116
        if(max > 9){
            ESP_LOGE(TAG_NTP, "REINICIANDO. MAXIMOS INTENTOS ALCANZADOS");
            return;
        }
        ESP_LOGI(TAG_NTP, "Esperando la sincronización con el servidor NTP...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        time(&t);
        localtime_r(&t, &timeinfo);
        max++;
    }
    ESP_LOGI(TAG_NTP, "Fecha y hora actualizada: %s", asctime(&timeinfo));
}

//Liberar memoria siempre que se utilice
    //cJSON_Delete(json);
    //free(json_str);

void jsonSrtInit(cJSON** json){
    *json = cJSON_CreateObject();
    cJSON_AddStringToObject(*json, "sensor", "");
    cJSON_AddNumberToObject(*json, "value", 0.0);
    cJSON_AddNumberToObject(*json, "sensorID", -1);
    cJSON_AddStringToObject(*json, "date", "");
}

//PRIMERO NECESITAMOS ELIMINAR LOS CAMPOS, PARA LUEGO ACTUALIZARLOS. SE DEBE A LA LIBERACION DE MEMORIA
void jsonStrLog(cJSON* json,char** json_str, const SharedMemoryLOG* data){
    //Eliminamos
    cJSON_DeleteItemFromObject(json, "sensor");
    cJSON_DeleteItemFromObject(json, "value");
    cJSON_DeleteItemFromObject(json, "date");
    cJSON_DeleteItemFromObject(json, "sensorID");
    //Añadimos
    cJSON_AddStringToObject(json, "sensor", data->sensor);
    cJSON_AddNumberToObject(json, "value", data->value);
    cJSON_AddNumberToObject(json, "sensorID", data->sensorID);
    cJSON_AddStringToObject(json, "date", data->dateTimeString);
    *json_str = cJSON_Print(json);
}

//================================================================
//==========================SENSORS===============================
//================================================================
//TODO SI SE QUEDA SIN MEMORIA REINICIAR
void print_memory_usage() {
    // Obtener el uso de la memoria RAM libre
    uint32_t free_ram = esp_get_free_heap_size();
    
    // Imprimir los resultados con el formato correcto
    printf("Uso de memoria RAM libre: %lu bytes\n", free_ram);
}

void DoorSensorTask(void *pvParameters){

    while(1) {
        int level = gpio_get_level(DOOR_SERNSOR_PIN);
        
        xSemaphoreTake(mutexSensorLog, portMAX_DELAY);
        getDateTimeString(sharedLog->dateTimeString);
        strcpy(sharedLog->sensor, DOOR_SENSOR);
        sharedLog->sensorID = DOOR_ID;
        sharedLog->value = level;
        //printf("Valor %d\n", level);
        xQueueSend(logQ, sharedLog, portMAX_DELAY);
        xSemaphoreGive(mutexSensorLog);

        vTaskDelay(pdMS_TO_TICKS(100)); // Esperar 1 segundo entre lecturas
    }
}

void PresenceSensorTask(void *pvParameters){
    
    while(1){
        int motionDetected = gpio_get_level(PRESENCE_SENSOR_PIN);
        xSemaphoreTake(mutexSensorLog, portMAX_DELAY);
        getDateTimeString(sharedLog->dateTimeString);
        strcpy(sharedLog->sensor, PRESENCE_SENSOR);
        sharedLog->sensorID = PRESENCE_ID;
        sharedLog->value = motionDetected;
        xQueueSend(logQ, sharedLog, portMAX_DELAY);
        xSemaphoreGive(mutexSensorLog);
        vTaskDelay(pdMS_TO_TICKS(100)); // Esperar 1 segundo entre lecturas
    }
}

void GasSensorTask(void *pvParameters){

    while(1) {
        uint32_t adc_value = adc1_get_raw(GAS_SENSOR_PIN);

        xSemaphoreTake(mutexSensorLog, portMAX_DELAY);
        getDateTimeString(sharedLog->dateTimeString);
        strcpy(sharedLog->sensor, GAS_SENSOR);
        sharedLog->sensorID = GAS_ID;
        sharedLog->value = (float) adc_value;
        //printf("Concentración de gas: %ld ppm\n", adc_value);
        xQueueSend(logQ, sharedLog, portMAX_DELAY);
        xSemaphoreGive(mutexSensorLog);

        vTaskDelay(pdMS_TO_TICKS(200)); // Esperar 1 segundo entre lecturas
    }
}

//================================================================
//=============================MQTT===============================
//================================================================

void publishValues(esp_mqtt_client_handle_t client, SharedMemoryLOG *copySharedLog, cJSON *json, char *json_str){
    
    if(xQueueReceive(logQ, copySharedLog, portMAX_DELAY) == pdTRUE){    

        jsonStrLog(json, &json_str, copySharedLog);
        switch (copySharedLog->sensorID){
        case DOOR_ID:
            esp_mqtt_client_publish(client, DOOR_TOPIC_VALUE, json_str,  0,  0,  0);
            break;
        
        case GAS_ID:
            esp_mqtt_client_publish(client, GAS_TOPIC_VALUE, json_str,  0,  0,  0);
            break;

        case PRESENCE_ID:
            esp_mqtt_client_publish(client, PRESENCE_TOPIC_VALUE, json_str,  0,  0,  0);
            break;

        default:
            ESP_LOGE(TAG_MQTT, "Error ID Sensors");
            printf("ID %d\n", copySharedLog->sensorID);
            break;
        }
        free(json_str);
        json_str = NULL;
         vTaskDelay(pdMS_TO_TICKS(400));
    }
}

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
            //mqtt_connected = 0;
            vTaskDelay(100);
            esp_restart();
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
        .credentials.client_id = "ESP32",
        .broker.address.uri = MQTT_BROKER,
        .broker.address.port = MQTT_PORT,
        
        .broker.verification.common_name = SERVER_CN,
        .broker.verification.certificate = (const char *) MQTTca_crt_start,
        .credentials.authentication.certificate = (const char *) sensorclient_crt_start,
        .credentials.authentication.key = (const char *) sensorclient_key_start,
        

        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD, 
        .credentials.authentication.use_secure_element = false,
        
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
    
    cJSON* json = NULL;
    char* json_str = NULL;
    //Inicializamos json
    jsonSrtInit(&json);

    esp_mqtt_client_subscribe(client, "/server",  0);
    esp_mqtt_client_publish(client, "/sensors/log", "DEBUG", 5, 0, 0);
    SharedMemoryLOG *copySharedLog = (SharedMemoryLOG *)malloc(sizeof(SharedMemoryLOG));

    while (1){
        publishValues(client, copySharedLog, json, json_str);
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

    // Si quiero ver la ram, colocar esto en un while 1 print_memory_usage();
    //================================================================
    //==========================DEBUG CONFIG==========================
    //================================================================

    //=================================================================


    sharedLog = (SharedMemoryLOG *)malloc(sizeof(SharedMemoryLOG));
    mutexSensorLog = xSemaphoreCreateMutex();
    logQ = xQueueCreate(3, sizeof(SharedMemoryLOG));

    //================================================================
    //=======================WIFI INITIALITATION======================
    //================================================================
    //almacenamiento no volátil (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //stack de TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());

    //controlador WiFi
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

    //fecha y hora
    DateTime();

    //================================================================
    //==========================GPIO CONFIG===========================
    //================================================================
    // Configurar el pìnes como entrada digital
    gpio_config_t io_conf;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pin_bit_mask = (1ULL << DOOR_SERNSOR_PIN) | (1ULL << PRESENCE_SENSOR_PIN);
    gpio_config(&io_conf);

    //ADC1
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(GAS_SENSOR_PIN, ADC_ATTEN_DB_11);


    //================================================================
    //=============================TASKS==============================
    //================================================================

    
    //Sensors Tasks
    xTaskCreatePinnedToCore(&DoorSensorTask, "DoorSensorTask",4096, NULL, 5, NULL, 1); 
    xTaskCreatePinnedToCore(&GasSensorTask, "GasSensorTask",4096, NULL, 5, NULL, 1); 
    xTaskCreatePinnedToCore(&PresenceSensorTask, "PresenceSensorTask",4096, NULL, 5, NULL, 1); 

    //MQTT Task en el Core0
    xTaskCreatePinnedToCore(&mqttTask, "mqtt_task", 4096, NULL, 5, NULL, 0);
}