#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "defines.h"
#include "time.h"
#include "sys/time.h"
#include "driver/adc.h"
#include <lwip/apps/sntp.h>
#include <cJSON.h>

#include "esp_system.h" // Para esp_chip_info
#include "spi_flash_mmap.h" // Para spi_flash_get_chip_size, spi_flash_get_free_size
#include "esp_task_wdt.h" // Para esp_task_wdt_get_task_load

static const char *TAG_MQTT = "MQTT";
static const char *TAG_WIFI = "Wifi";
static const char *TAG_NTP = "NTP";

int mqtt_connected = 0;

//================================================================
//======================SHARED MEMORY=============================
//================================================================
typedef struct {
    time_t time;
    char sensor[12];

} SharedMemory;

typedef struct {
    char dateTimeString[64];
    char sensor[12];
    float value;

} SharedMemoryLOG;
SharedMemoryLOG *sharedLog = NULL;

QueueHandle_t dataQ;
QueueHandle_t dataLOG;



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
    cJSON_AddStringToObject(*json, "date", "");
}

//PRIMERO NECESITAMOS ELIMINAR LOS CAMPOS, PARA LUEGO ACTUALIZARLOS. SE DEBE A LA LIBERACION DE MEMORIA
void jsonStrLog(cJSON* json,char** json_str, const SharedMemoryLOG* data){
    //Eliminamos
    cJSON_DeleteItemFromObject(json, "sensor");
    cJSON_DeleteItemFromObject(json, "value");
    cJSON_DeleteItemFromObject(json, "date");
    //Añadimos
    cJSON_AddStringToObject(json, "sensor", data->sensor);
    cJSON_AddNumberToObject(json, "value", data->value);
    cJSON_AddStringToObject(json, "date", data->dateTimeString);
    *json_str = cJSON_Print(json);
}

//================================================================
//==========================SENSORS===============================
//================================================================
//TODO SI SE QUEDA SIN MEMORIA REINICIAR
void print_memory_usage() {
    //esp_chip_info_t chip_info;
    //esp_chip_info(&chip_info);
    
    // Obtener el uso de la memoria RAM libre
    uint32_t free_ram = esp_get_free_heap_size();
    
    // Obtener el uso de la memoria flash
    //uint32_t total_flash = spi_flash_get_chip_size();
    //uint32_t free_flash = spi_flash_get_free_size();
    //uint32_t used_flash = total_flash - free_flash;
    
    // Nota: La función esp_task_wdt_get_task_load() no es adecuada para obtener el uso de CPU.
    // Necesitarías un enfoque diferente para obtener el uso de CPU, como medir el tiempo de ejecución de tus tareas.
    // Aquí se muestra un marcador de posición para la carga de CPU.
    //uint32_t cpu_load =   0; // Este valor debe ser calculado de manera diferente

    // Imprimir los resultados con el formato correcto
    printf("Uso de memoria RAM libre: %lu bytes\n", free_ram);
    //printf("Uso de memoria flash: %lu bytes usados de %lu bytes totales\n", used_flash, total_flash);
    //printf("Uso de CPU: %lu%%\n", cpu_load);
}

void DoorSensorTask(void *pvParameters){
    // Inicializar ADC2
    //adc2_config_channel_atten(ADC2_CHANNEL_0, ADC_ATTEN_DB_0);  // Configurar la atenuación del canal ADC2_0

    // Leer valores analógicos de forma continua
    while(1) {
        // Leer el valor analógico del sensor
        //uint32_t adc_value = 0;
        //adc2_get_raw(ADC2_CHANNEL_0, ADC_WIDTH_BIT_12, &adc_value);
        //printf("Valor del sensor: %ld\n", adc_value);
        print_memory_usage();
        vTaskDelay(pdMS_TO_TICKS(10000)); // Esperar 1 segundo entre lecturas
    }
}

void PresenceSensorTask(void *pvParameters){
    
    // Configurar el pin del sensor como entrada
    gpio_config_t io_conf;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_13);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    
    while(1){
        int motionDetected = gpio_get_level(GPIO_NUM_13);
        if (motionDetected) {
            printf("¡Movimiento detectado!\n");
        } else {
            printf("No se detecta movimiento.\n");
        }
        vTaskDelay(pdMS_TO_TICKS(3000)); // Esperar 1 segundo entre lecturas
    }
}

void GasSensorTask(void *pvParameters){
    // Configurar el canal ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);

    while(1) {
        // Leer el valor analógico del sensor MQ-2
        uint32_t adc_value = adc1_get_raw(ADC1_CHANNEL_0);
        
        // Calcular la concentración de gas basada en el valor del ADC
        // (requiere calibración específica para tu sensor)
        sharedLog->value = (float) adc_value;
        // Imprimir el valor de concentración de gas
        //printf("Concentración de gas: %ld ppm\n", adc_value);

        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo entre lecturas
    }
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
    

    cJSON* json = NULL;
    char* json_str = NULL;
    //Inicializamos json
    jsonSrtInit(&json);

    esp_mqtt_client_subscribe(client, "/server",  0);
    
    while (1){
        getDateTimeString(sharedLog->dateTimeString);
        strcpy(sharedLog->sensor, GAS_SENSOR);
        jsonStrLog(json, &json_str, sharedLog);
        esp_mqtt_client_publish(client, "/sensors/log", json_str,  0,  0,  0);
        vTaskDelay(200);
        free(json_str);
        json_str = NULL;
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
    
    esp_log_level_set(TAG_WIFI, ESP_LOG_DEBUG);
    sharedLog = (SharedMemoryLOG *)malloc(sizeof(SharedMemoryLOG));
    
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

    //Actualizar fecha y hora
    DateTime();
    //MQTT Task en el Core0
    xTaskCreatePinnedToCore(&mqttTask, "mqtt_task", 4096, NULL, 5, NULL, 0);
    
    //Sensors Tasks
    xTaskCreatePinnedToCore(&DoorSensorTask, "DoorSensorTask",4096, NULL, 5, NULL, 1); 
    xTaskCreatePinnedToCore(&GasSensorTask, "GasSensorTask",4096, NULL, 5, NULL, 1); 
    xTaskCreatePinnedToCore(&PresenceSensorTask, "PresenceSensorTask",4096, NULL, 5, NULL, 1); 

}