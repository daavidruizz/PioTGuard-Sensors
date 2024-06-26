/*
 * PioTGuard-Sensors
 * David Ruiz Luque
 * Universidad de Malaga
 * daavidruiz01@outlook.com
 * 2024-06-05
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "nvs.h"
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
#include "nvs_write.h"

#define MAX_LENGTH 32

static const char *TAG_MQTT = "MQTT";
static const char *TAG_WIFI = "Wifi";
static const char *TAG_NTP = "NTP";

int mqtt_connected = 0;

//================================================================
//============================DEBUG===============================
//================================================================


//================================================================
//=======================CFG SETTINGS=============================
//================================================================
int8_t INFO_REQ = 0;
int8_t REBOOT_FLAG = 0;

typedef struct {
   int8_t alarmSet;
   int8_t doorSet;
   int8_t presenceSet;
   int8_t gasSet;
} SharedSettings;

SharedSettings *settings = NULL;
SemaphoreHandle_t mutexSettings;

//NVS VALUES
size_t required_size = MAX_LENGTH;
char ssid[MAX_LENGTH];
char ssid_pass[MAX_LENGTH];

char cn[MAX_LENGTH];
char key_pass[MAX_LENGTH];

char mqttUser[MAX_LENGTH];
char mqttPass[MAX_LENGTH];

//================================================================
//=======================CERTIFICATES=============================
//================================================================
extern const uint8_t MQTTca_crt_start[] asm("_binary_mqtt_ca_crt_start");
extern const uint8_t sensorclient_crt_start[] asm("_binary_sensor_crt_start");
extern const uint8_t sensorclient_key_start[] asm("_binary_sensor_key_start");

//extern const uint8_t binary_MQTTca_crt_start[];
//extern const uint8_t binary_sensorclient_crt_start[];
//extern const uint8_t binary_sensorclient_key_start[];

//================================================================
//======================SHARED MEMORY=============================
//================================================================
typedef struct {
    char dateTimeString[64];
    char sensor[16];
    int8_t sensorID;
    float value;
    int8_t enabled;
} SharedMemory;

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
    //zona horaria a CET/CEST para España
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init(); //Horario verano
    time_t t = 0;
    struct tm timeinfo = { 0 };
    
    int8_t max = 0;
    while (timeinfo.tm_year < (2016 - 1900)) { //116
        if(max > 15){
            ESP_LOGE(TAG_NTP, "REINICIANDO. MAXIMOS INTENTOS ALCANZADOS");
            esp_restart();
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

/*ALL cJSON initialisation */
void jsonSrtInit(cJSON **jsonDebug, cJSON **jsonSettings){
    
    *jsonDebug = cJSON_CreateObject();
    cJSON_AddNumberToObject(*jsonDebug, "enabled", -1);
    cJSON_AddStringToObject(*jsonDebug, "sensor", "");
    cJSON_AddNumberToObject(*jsonDebug, "value", 0.0);
    cJSON_AddNumberToObject(*jsonDebug, "sensorID", -1);
    cJSON_AddStringToObject(*jsonDebug, "date", "");

    *jsonSettings = cJSON_CreateObject();
    cJSON_AddNumberToObject(*jsonSettings, "alarm", 0);
    cJSON_AddNumberToObject(*jsonSettings, "door", 0);
    cJSON_AddNumberToObject(*jsonSettings, "gas", 0);
    cJSON_AddNumberToObject(*jsonSettings, "presence", 0);
}

//PRIMERO NECESITAMOS ELIMINAR LOS CAMPOS, PARA LUEGO ACTUALIZARLOS. SE DEBE A LA LIBERACION DE MEMORIA
void jsonSrtSettings(cJSON* json,char** json_str, const SharedSettings* settings){
    //Eliminamos
    cJSON_DeleteItemFromObject(json, "alarm");
    cJSON_DeleteItemFromObject(json, "door");
    cJSON_DeleteItemFromObject(json, "gas");
    cJSON_DeleteItemFromObject(json, "presence");
    //Añadimos
    xSemaphoreTake(mutexSettings, portMAX_DELAY);
    cJSON_AddNumberToObject(json, "alarm", settings->alarmSet);
    cJSON_AddNumberToObject(json, "door", settings->doorSet);
    cJSON_AddNumberToObject(json, "gas", settings->gasSet);
    cJSON_AddNumberToObject(json, "presence", settings->presenceSet);
    *json_str = cJSON_Print(json);
    xSemaphoreGive(mutexSettings);
}

void jsonStrLog(cJSON* json,char** json_str, const SharedMemory* data){
    //Eliminamos
    cJSON_DeleteItemFromObject(json, "enabled");
    cJSON_DeleteItemFromObject(json, "sensor");
    cJSON_DeleteItemFromObject(json, "value");
    cJSON_DeleteItemFromObject(json, "date");
    cJSON_DeleteItemFromObject(json, "sensorID");
    //Añadimos
    cJSON_AddNumberToObject(json, "enabled", data->enabled);
    cJSON_AddStringToObject(json, "sensor", data->sensor);
    cJSON_AddNumberToObject(json, "value", data->value);
    cJSON_AddNumberToObject(json, "sensorID", data->sensorID);
    cJSON_AddStringToObject(json, "date", data->dateTimeString);
    *json_str = cJSON_Print(json);
}

//================================================================
//==========================SENSORS===============================
//================================================================

int DOOR_TRIGGER = 0;
int PRESENCE_TRIGGER = 0;
int GAS_TRIGGER = 0;

//TODO SI SE QUEDA SIN MEMORIA REINICIAR
void print_memory_usage() {
    // Obtener el uso de la memoria RAM libre
    uint32_t free_ram = esp_get_free_heap_size();
    
    // Imprimir los resultados con el formato correcto
    printf("Uso de memoria RAM libre: %lu bytes\n", free_ram);
}

void DoorSensorTask(void *pvParameters){
    //TODO INVERTIR VALOS SENSOR DE 0 A 1
    SharedMemory dataDoor;

    int previousLevel;
    int currentLevel;
    int sendLevel = -1;
    TickType_t lastChangeTime = xTaskGetTickCount();
    TickType_t lastSendTime = xTaskGetTickCount();

    while(1) {

        previousLevel = gpio_get_level(DOOR_SERNSOR_PIN);
        vTaskDelay(pdMS_TO_TICKS(50)); //Debounce
        currentLevel = gpio_get_level(DOOR_SERNSOR_PIN);

        //El tiempo de espera es simplemente para asegurar que se activa la alarma aunque no cambie el valor del sensor.
        int hasTimeElapsed = ((xTaskGetTickCount() - lastChangeTime) >= pdMS_TO_TICKS(1500));
        int hasTimeElapsedForSend = ((xTaskGetTickCount() - lastSendTime) >= pdMS_TO_TICKS(3000)); // 3 segundos

        if((previousLevel != currentLevel) || hasTimeElapsed){
            
            previousLevel = currentLevel;
            lastChangeTime = xTaskGetTickCount();
            
            //Trigger the alarm        
            getDateTimeString(dataDoor.dateTimeString);
            strcpy(dataDoor.sensor, DOOR_SENSOR);
            dataDoor.sensorID = DOOR_ID;
            dataDoor.value = currentLevel;
            dataDoor.enabled = settings->doorSet;
            DOOR_TRIGGER = currentLevel; //TRIGGER THE ALARM
            
            if((currentLevel != sendLevel  || hasTimeElapsedForSend) && !currentLevel){
                lastSendTime = xTaskGetTickCount();
                xQueueSend(dataQ, &dataDoor, portMAX_DELAY);
            }
            sendLevel = dataDoor.value;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void PresenceSensorTask(void *pvParameters){
    
    SharedMemory dataPresence;
    TickType_t lastChangeTime = xTaskGetTickCount();

    while(1){
        int motionDetected = gpio_get_level(PRESENCE_SENSOR_PIN);
        int hasTimeElapsed = ((xTaskGetTickCount() - lastChangeTime) >= pdMS_TO_TICKS(1500));

        getDateTimeString(dataPresence.dateTimeString);
        strcpy(dataPresence.sensor, PRESENCE_SENSOR);
        dataPresence.sensorID = PRESENCE_ID;
        dataPresence.value = motionDetected;
        dataPresence.enabled = settings->presenceSet;
        PRESENCE_TRIGGER = motionDetected;

        if(motionDetected && hasTimeElapsed){
            lastChangeTime = xTaskGetTickCount();
            xQueueSend(dataQ, &dataPresence, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

void GasSensorTask(void *pvParameters){
    //TODO: MANAGE SENSIBILITY GAS SENSOR
    SharedMemory dataGas;
    TickType_t lastChangeTime = xTaskGetTickCount();

    while(1) {
        uint32_t adc_value = adc1_get_raw(GAS_SENSOR_PIN);
        int hasTimeElapsed = ((xTaskGetTickCount() - lastChangeTime) >= pdMS_TO_TICKS(1500));

        getDateTimeString(dataGas.dateTimeString);
        strcpy(dataGas.sensor, GAS_SENSOR);
        dataGas.sensorID = GAS_ID;
        dataGas.value = (float) adc_value;
        dataGas.enabled = settings->gasSet;
        //printf("Concentración de gas: %ld ppm\n", adc_value);
        GAS_TRIGGER = adc_value;
        //TODO SENSIBILITY GAS SENSOR
        if(hasTimeElapsed){
            lastChangeTime = xTaskGetTickCount();
            xQueueSend(dataQ, &dataGas, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

//================================================================
//=============================MQTT===============================
//================================================================

void publishSettings(esp_mqtt_client_handle_t client, SharedSettings *settings, cJSON *json, char *json_str){
    jsonSrtSettings(json, &json_str, settings);
    esp_mqtt_client_publish(client, DEVICE_SENSORS, json_str,  0,  2,  0);
    free(json_str);
}

void publishValues(esp_mqtt_client_handle_t client, SharedMemory *data , cJSON *json, char *json_str){

    
    if(xQueueReceive(dataQ, data, portMAX_DELAY) == pdTRUE){    

        if(settings->alarmSet){
            jsonStrLog(json, &json_str, data);
            
            switch (data->sensorID){
                case DOOR_ID:
                    if(settings->doorSet == 1 && !DOOR_TRIGGER){
                        esp_mqtt_client_publish(client, ALARM_TRIGGER, json_str,  0,  2,  0);
                    }
                    break;

                case GAS_ID:
                    if(settings->gasSet == 1 && GAS_TRIGGER > 2000){
                        esp_mqtt_client_publish(client, ALARM_TRIGGER, json_str,  0,  2,  0);
                    }
                    break;

                case PRESENCE_ID:
                    if(settings->presenceSet == 1 && PRESENCE_TRIGGER){
                        esp_mqtt_client_publish(client, ALARM_TRIGGER, json_str,  0,  2,  0);
                    }
                    break;

                default:
                    ESP_LOGE(TAG_MQTT, "Error ID Sensors");
                    printf("ID %d\n", data->sensorID);
                    break;
            }

            free(json_str);
            json_str = NULL;
        }
    }
}

int topicToID(char *topic, int topic_len){
    
    int id = -1;

    char topic_buffer[topic_len + 1];
    memcpy(topic_buffer, topic, topic_len);
    topic_buffer[topic_len] = '\0';

    if(strcmp(topic_buffer, CFG_ALARM) == 0){
        id = ALARM_ID;
    }else if(strcmp(topic_buffer, CFG_ALL) == 0){
        id = ALL_ID; //ALL CFG
    }else if(strcmp(topic_buffer, CFG_DOOR) == 0){
        id = DOOR_ID; 
    }else if(strcmp(topic_buffer, CFG_GAS) == 0){
        id = GAS_ID;
    }else if(strcmp(topic_buffer, CFG_PRESENCE) == 0){
        id = PRESENCE_ID;
    }else if(strcmp(topic_buffer, REQ_INFO) == 0){
        id = REQ_INFO_ID; //REQUEST INFO
    }else if(strcmp(topic_buffer, REBOOT) == 0){
        id = REBOOT_ID;
    }else{
        id = -1;
        printf("ERROR: Unidentified topic.\n");
        printf(topic_buffer);
    }
    return id;
}
//TODO SOLUCIONAR SI NO LLEGA UN JSON
void topicHandler(char *topic, int topic_len, char *data, int data_len){
    
    cJSON *dataJSON = NULL;
    dataJSON = cJSON_Parse(data);

    //Debug
    printf("Tema: %.*s\n", topic_len, topic);
    printf("%.*s\n", data_len, data);

    cJSON *json = cJSON_GetObjectItem(dataJSON, "value");

    if(json == NULL){
        printf("JSON error, Not a JSON\n");
        free(dataJSON);
        free(json);
        return;
    }

    int value = json->valueint;

    nvs_handle_t flash;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &flash);
    
    if (err != ESP_OK){
        printf("CAN'T OPEN THE NVS\n");
        free(dataJSON);
        free(json);
        return;
    }

    /*value MUST BE 1 or 0*/
    if(value == 1 || value == 0){

        int id = topicToID(topic, topic_len);

        xSemaphoreTake(mutexSettings, portMAX_DELAY);
        switch (id) {
            case ALARM_ID:
                settings->alarmSet = value;
                ESP_ERROR_CHECK(nvs_set_i8(flash, NVS_ALARM, value));
                INFO_REQ = 1;
                break;
            case ALL_ID:
                settings->doorSet = value;
                settings->gasSet = value;
                settings->presenceSet = value;
                ESP_ERROR_CHECK(nvs_set_i8(flash, NVS_DOOR, value));
                ESP_ERROR_CHECK(nvs_set_i8(flash, NVS_GAS, value));
                ESP_ERROR_CHECK(nvs_set_i8(flash, NVS_PRESENCE, value));
                INFO_REQ = 1;
                break;
            case DOOR_ID:
                settings->doorSet = value;
                ESP_ERROR_CHECK(nvs_set_i8(flash, NVS_DOOR, value));
                INFO_REQ = 1;
                break;
            case GAS_ID:
                settings->gasSet = value;
                ESP_ERROR_CHECK(nvs_set_i8(flash, NVS_GAS, value));
                INFO_REQ = 1;
                break;
            case PRESENCE_ID:
                settings->presenceSet = value;
                ESP_ERROR_CHECK(nvs_set_i8(flash, NVS_PRESENCE, value));
                INFO_REQ = 1;
                break;
            case REQ_INFO_ID:
                INFO_REQ = value;
                break;
            case REBOOT_ID:
                REBOOT_FLAG = value;
                break;
            default:
                printf("ERROR: Unidentified topic.\n");
                break;
        }
        xSemaphoreGive(mutexSettings);

        if (err != ESP_OK) {
            nvs_close(flash);
            free(dataJSON);
            free(json);
            return;
        }
    }else{
        printf("ERROR. INVALID VALUE OF SETTINGS\n");
    }
    nvs_close(flash);
    free(dataJSON);
    free(json);
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
            topicHandler(event->topic, event->topic_len, event->data, event->data_len);
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
        .broker.verification.common_name = cn,
        .broker.verification.certificate = (const char *) MQTTca_crt_start,
        .credentials.authentication.certificate = (const char *) sensorclient_crt_start,
        .credentials.authentication.key = (const char *) sensorclient_key_start,
        .credentials.authentication.key_password = key_pass,
        .credentials.username = mqttUser,
        .credentials.authentication.password = mqttPass, 
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
    
    cJSON* jsonLOG = NULL;
    char* jsonStrLOG = NULL;

    cJSON *jsonSettings = NULL;
    char *jsonStrSettings = NULL;

    //Inicializamos json
    jsonSrtInit(&jsonLOG, &jsonSettings);

    /*SUBSCRIPTIONS*/
    esp_mqtt_client_subscribe(client, REQ_INFO,  2);

    esp_mqtt_client_subscribe(client, CFG_ALARM,  2);
    esp_mqtt_client_subscribe(client, CFG_DOOR,  2);
    esp_mqtt_client_subscribe(client, CFG_GAS,  2);
    esp_mqtt_client_subscribe(client, CFG_PRESENCE,  2);
    esp_mqtt_client_subscribe(client, CFG_ALL,  2);
    esp_mqtt_client_subscribe(client, REBOOT,  2);

    esp_mqtt_client_publish(client, DEVICE_INFO, "POWERED ON", 10, 2, 0);
    
    SharedMemory dataToSend;

    while (1){

        publishValues(client, &dataToSend, jsonLOG, jsonStrLOG);
        
        if(INFO_REQ){
            publishSettings(client, settings, jsonSettings, jsonStrSettings);
            INFO_REQ = 0;
        }

        if(REBOOT_FLAG){
            printf("REBOOTING...\n");
            esp_mqtt_client_publish(client, DEVICE_INFO, "REBOOTING", 9, 2, 0);
            REBOOT_FLAG = 0;
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
        //print_memory_usage();
        /*CONTROL LED AND WAIT TO REFRESH APP*/
        // Encender el LED
        gpio_set_level(LED_GPIO_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));  // Esperar 150 ms
        // Apagar el LED
        gpio_set_level(LED_GPIO_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(950)); //1s para que cargue bien todo el refresco de la app
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

    #ifdef WRITE_MODE
    esp_err_t ok;

    ok = nvs_write_init();
    ok = nvs_write();
    if(ok == ESP_OK){
        printf("NVS WRITED\n");
    }else{
        printf("ERROR WRITING NVS\n");
    }

    #elif READ_MODE

    // Si quiero ver la ram, colocar esto en un while 1 print_memory_usage();
    //================================================================
    //==========================DEBUG CONFIG==========================
    //================================================================

    //=================================================================

    settings = (SharedSettings *)malloc(sizeof(SharedSettings));

    //settings->alarmSet = 0;
    //settings->doorSet = 0;
    //settings->gasSet = 0;
    //settings->presenceSet = 0;

    mutexSettings = xSemaphoreCreateMutex();

    dataQ = xQueueCreate(9, sizeof(SharedMemory));

    //================================================================
    //================================NVS=============================
    //================================================================
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t flash;
    ret = nvs_open("storage", NVS_READONLY, &flash);
    if (ret != ESP_OK) {
        printf("Error (%s) initializing NVS!\n", esp_err_to_name(ret));
        return;
    } 


    required_size = MAX_LENGTH;
    ESP_ERROR_CHECK(nvs_get_str(flash, SSID, ssid, &required_size));
    //printf(SSID": %s; Length: %d\n", ssid, required_size);

    required_size = MAX_LENGTH;
    ESP_ERROR_CHECK(nvs_get_str(flash, SSID_PASS, ssid_pass, &required_size));
    //printf(SSID_PASS": %s; Length: %d\n", ssid_pass, required_size);

    required_size = MAX_LENGTH;
    ESP_ERROR_CHECK(nvs_get_str(flash, CN, cn, &required_size));
    //printf(CN": %s; Length: %d\n", cn, required_size);

    required_size = MAX_LENGTH;
    ESP_ERROR_CHECK(nvs_get_str(flash, KEY_PASS, key_pass, &required_size));
    //printf(KEY_PASS": %s; Length: %d\n", key_pass, required_size);

    required_size = MAX_LENGTH;
    ESP_ERROR_CHECK(nvs_get_str(flash, MQTT_USER, mqttUser, &required_size));
    //printf(MQTT_USER": %s; Length: %d\n", mqttUser, required_size);

    required_size = MAX_LENGTH;
    ESP_ERROR_CHECK(nvs_get_str(flash, MQTT_PASS, mqttPass, &required_size));
    //printf(MQTT_PASS": %s; Length: %d\n", mqttPass, required_size);

    
    ESP_ERROR_CHECK(nvs_get_i8(flash, NVS_ALARM, &settings->alarmSet));
    ESP_ERROR_CHECK(nvs_get_i8(flash, NVS_DOOR, &settings->doorSet));
    ESP_ERROR_CHECK(nvs_get_i8(flash, NVS_GAS, &settings->gasSet));
    ESP_ERROR_CHECK(nvs_get_i8(flash, NVS_PRESENCE, &settings->presenceSet));
    

    nvs_close(flash);
    
    //================================================================
    //=======================WIFI INITIALITATION======================
    //================================================================
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
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, ssid_pass);
    /*
    // Configuración y conexión a la red WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = (unsigned char)ssid,
            .password = (unsigned char)ssid_pass,
        },
    };
    */
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

    // Configurar el pin GPIO2 como salida digital
    esp_rom_gpio_pad_select_gpio(LED_GPIO_PIN);
    gpio_set_direction(LED_GPIO_PIN, GPIO_MODE_OUTPUT);
    //ADC1
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(GAS_SENSOR_PIN, ADC_ATTEN_DB_11);


    //================================================================
    //=============================TASKS==============================
    //================================================================

    
    //Sensors Tasks
    xTaskCreatePinnedToCore(&DoorSensorTask, "DoorSensorTask",4096, NULL, 6, NULL, 1); 
    xTaskCreatePinnedToCore(&GasSensorTask, "GasSensorTask",4096, NULL, 6, NULL, 1); 
    xTaskCreatePinnedToCore(&PresenceSensorTask, "PresenceSensorTask",4096, NULL, 6, NULL, 1); 

    //MQTT Task en el Core0
    xTaskCreatePinnedToCore(&mqttTask, "mqtt_task", 4096, NULL, 7, NULL, 0);
    #endif
}