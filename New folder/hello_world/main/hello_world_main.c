#include <stdio.h>
#include "sdkconfig.h"
#include "nvs_flash.h"
#include <time.h>
#include <sys/time.h>

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "mqtt_client.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_err.h"

#include "esp_random.h"

#include "driver/uart.h"
#include "driver/i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"

#include "common.h"
#include "i2cdev.h"
#include <sht4x.h>
#include "sds011.h"
#include "ds3231.h"

#define PERIOD 1
#define SDS_PERIOD 1
#define SDS_SLEEP 15

#define WIFI_TAG        "Esp_Wifi"
#define TAG_MQTT        "Esp_MQTT"
#define TAG             "DO_AN_2"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1
#define MQTT_CONNECTED_BIT WIFI_CONNECTED_BIT
#define MQTT_DISCONNECTED_BIT WIFI_DISCONNECTED_BIT


#define SSID CONFIG_SSID
#define PASS CONFIG_PASSWORD

#define SDS011_UART_PORT UART_NUM_2
#define SDS011_RX_GPIO 16
#define SDS011_TX_GPIO 17

#define SDA_PIN 21
#define SCL_PIN 22

TaskHandle_t test_sht41 = NULL;
TaskHandle_t test_ds3231 = NULL;
TaskHandle_t test_sds011 = NULL;
TaskHandle_t data_task = NULL;
TaskHandle_t send_data_mqtt_task = NULL;


QueueHandle_t dataSensor_queue;
QueueHandle_t data_mqtt_queue;

SemaphoreHandle_t I2C_mutex = NULL;
SemaphoreHandle_t sentDataToMQTT_semaphore = NULL;

static EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t mqtt_event_group;
esp_mqtt_client_handle_t mqttClient_handle = NULL;

uint8_t MAC_address[6];
sht4x_t dev;
i2c_dev_t ds3231;

struct tm init_time = {

};

volatile uint32_t count_restart = 0;
volatile data_type data;


//const char *Data_MQTT_String = "{\n\t\"Time_real_Date\":%s,\n\t\"temperature\":%.2f,\n\t\"humidity\":%.2f,\n\t\"PM2_5\":%.2f,\n\t\"PM10\":%.2f\n}";

static void initialize_nvs(void)
{
	esp_err_t error = nvs_flash_init();
	if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
		error = nvs_flash_init();
	}
	ESP_ERROR_CHECK_WITHOUT_ABORT(error);
}

static const struct sds011_tx_packet sds011_tx_sleep_packet = {
    .head = SDS011_PACKET_HEAD,
    .command = SDS011_CMD_TX,
    .sub_command = SDS011_TX_CMD_SLEEP_MODE,
    .payload_sleep_mode = {.method = SDS011_METHOD_SET,
                           .mode = SDS011_SLEEP_MODE_ENABLED},
    .device_id = SDS011_DEVICE_ID_ALL,
    .tail = SDS011_PACKET_TAIL};

static const struct sds011_tx_packet sds011_tx_wakeup_packet = {
    .head = SDS011_PACKET_HEAD,
    .command = SDS011_CMD_TX,
    .sub_command = SDS011_TX_CMD_SLEEP_MODE,
    .payload_sleep_mode = {.method = SDS011_METHOD_SET,
                           .mode = SDS011_SLEEP_MODE_DISABLED},
    .device_id = SDS011_DEVICE_ID_ALL,
    .tail = SDS011_PACKET_TAIL};

void send_data_mqtt(void *arg){
	data_type MQTT_data;
	while(1){
		if(xEventGroupWaitBits(mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY) && MQTT_CONNECTED_BIT){
			if(uxQueueMessagesWaiting(data_mqtt_queue) != 0){
				if (xQueueReceive(data_mqtt_queue, &MQTT_data, portMAX_DELAY) == pdPASS) {
					ESP_LOGI(__func__, "MQTT data waiting to read %d, Available space %d", uxQueueMessagesWaiting(data_mqtt_queue), uxQueueSpacesAvailable(data_mqtt_queue));
					ESP_LOGI(__func__,"MQTT Data: %.2f °C, %.2f %%", MQTT_data.temp, MQTT_data.humi);
					if(xSemaphoreTake(sentDataToMQTT_semaphore, portMAX_DELAY) == pdTRUE) {
						esp_err_t error = 0;
						int msg_id;
						WORD_ALIGNED_ATTR char mqttMessage[256];
						sprintf(mqttMessage, "{\n\t\"Time_real_Date\":\"%02d/%02d/%02d %02d:%02d:%02d\",\n\t\"temperature\":%.2f,\n\t\"humidity\":%.2f,\n\t\"PM2_5\":%.2f,\n\t\"PM10\":%.2f\n}",
								MQTT_data.time_year,
								MQTT_data.time_month,
								MQTT_data.time_day,
								MQTT_data.time_hour,
								MQTT_data.time_min,
								MQTT_data.time_sec,
								MQTT_data.temp,
								MQTT_data.humi,
								MQTT_data.PM2_5,
								MQTT_data.PM10);
						ESP_LOGI(__func__, "%s",mqttMessage);
						error = esp_mqtt_client_publish(mqttClient_handle, (const char *)CONFIG_MQTT_TOPIC, mqttMessage, 0, 0, 0);
						xSemaphoreGive(sentDataToMQTT_semaphore);
						if (error == ESP_FAIL){
							ESP_LOGE(__func__, "MQTT client publish message failed");
							msg_id = esp_mqtt_client_subscribe(mqttClient_handle, (const char *)CONFIG_MQTT_TOPIC, 0);
							ESP_LOGI(__func__, "sent subscribe unsuccessful, msg_id=%d", msg_id);

						} else {
							ESP_LOGI(__func__, "MQTT client publish message success");
							msg_id = esp_mqtt_client_subscribe(mqttClient_handle, (const char *)CONFIG_MQTT_TOPIC, 0);
							ESP_LOGI(__func__, "sent subscribe successful, msg_id=%d", msg_id);
						}
					}
//					vTaskDelay(pdMS_TO_TICKS(60000));
				}
			}
//			else {
//				vTaskDelay(pdMS_TO_TICKS(3 * 60000));
//			}
		}
	}
}

//mqtt
static void mqtt_event_handler (void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    ESP_LOGD(__func__, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    switch((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGI(__func__, "MQTT_EVENT_CONNECTED");
        printf("Topic: %s\n", CONFIG_MQTT_TOPIC);
        break;

        case MQTT_EVENT_DISCONNECTED:
        xEventGroupSetBits(mqtt_event_group, MQTT_DISCONNECTED_BIT);
        ESP_LOGE(__func__, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(__func__, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGE(__func__, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(__func__, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGE(__func__, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGE(__func__, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        }
        else
        {
            ESP_LOGW(__func__, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;

    default:
        ESP_LOGI(__func__, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(void) {
	printf("mqtt\n");
	mqtt_event_group = xEventGroupCreate();
    const esp_mqtt_client_config_t mqtt_Config = {
        .host = CONFIG_BROKER_HOST,
        .uri = CONFIG_BROKER_URI,
        .port = CONFIG_BROKER_PORT,
        .username = CONFIG_MQTT_USERNAME,
        .password = CONFIG_MQTT_PASSWORD,
    };
    ESP_LOGI(__func__, "Free memory: %d bytes", esp_get_free_heap_size());
    mqttClient_handle = esp_mqtt_client_init(&mqtt_Config);
     /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqttClient_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, mqttClient_handle);
    esp_mqtt_client_start(mqttClient_handle);
    esp_read_mac(MAC_address, ESP_MAC_WIFI_STA); // Get MAC address of ESP32
    xTaskCreatePinnedToCore(send_data_mqtt, "send_data_http", 2048 * 2, NULL, 4, &send_data_mqtt_task, tskNO_AFFINITY);
}

//wifi
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	printf("id %d\n", event_id);
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(WIFI_TAG,"WiFi connecting ... \n");
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(__func__, "Wi-Fi connected AP SSID:%s password:%s\n", SSID, PASS);

        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        ESP_LOGI(WIFI_TAG,"Try to WiFi connection ... \n");
        esp_wifi_connect();
        break;
    case IP_EVENT_STA_GOT_IP:
        ESP_LOGI(WIFI_TAG,"WiFi got IP ...");
        ESP_LOGI(WIFI_TAG,"MQTT start!");
        mqtt_app_start();
        break;
    default:
        break;
    }
}

void wifi_connection()
{
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = SSID,
            .password = PASS}};
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    esp_wifi_start();
    esp_wifi_connect();
}

void data_control_task(void *arg){
	printf("data\n");
	data_type temp = {};
	data_type result = {};
	while(1){
		vTaskDelay(pdMS_TO_TICKS(1000));
		temp.time_year = data.time_year;
		temp.time_month = data.time_month;
		temp.time_day = data.time_day;
		temp.time_hour = data.time_hour;
		temp.time_min = data.time_min;
		temp.time_sec = data.time_sec;

		temp.temp = data.temp;
		temp.humi = data.humi;
		temp.PM2_5 = data.PM2_5;
		temp.PM10 = data.PM10;

		ESP_LOGI(__func__,"Data Sensor: %.2f °C, %.2f %%", temp.temp, temp.humi);

		xQueueSendToBack(dataSensor_queue, (void *)&temp, 1000/portTICK_RATE_MS);
		ESP_LOGI(__func__, "Data waiting to read %d, Available space %d", uxQueueMessagesWaiting(dataSensor_queue), uxQueueSpacesAvailable(dataSensor_queue));


		if(xQueueReceive(dataSensor_queue, &result, 1000/portTICK_PERIOD_MS) == pdPASS){

			/* lay data hien thi LCD */

			xQueueSendToBack(data_mqtt_queue,&result, 1000/portMAX_DELAY);
			ESP_LOGI(__func__, "MQTT data waiting to read %d, Available space %d", uxQueueMessagesWaiting(data_mqtt_queue), uxQueueSpacesAvailable(data_mqtt_queue));
			wifi_connection();
			break;
		}
	}
	vTaskDelete(NULL);
}

//sh41
void sht41_task(void *pvParameters)
{
	printf("sht41\n");
	while(1){
		ESP_ERROR_CHECK(sht4x_init_desc(&dev, 0, SDA_PIN, SCL_PIN));
		ESP_ERROR_CHECK(sht4x_init(&dev));
		float temperature;
		float humidity;
		if (xSemaphoreTake(I2C_mutex, portMAX_DELAY) == pdTRUE)
		{
			ESP_LOGI(__func__, "SHT41 take semaphore");
			ESP_ERROR_CHECK(sht4x_measure(&dev, &temperature, &humidity));
			data.humi = humidity;
			data.temp = temperature;
			ESP_LOGI(__func__,"SHT4x Sensor: %.2f °C, %.2f %%", temperature, humidity);

			ESP_LOGI(__func__, "SHT41 give semaphore");

			ESP_ERROR_CHECK(sht4x_free_desc(&dev));
			xSemaphoreGive(I2C_mutex);
			break;
		}
	}
	xTaskCreatePinnedToCore(data_control_task, "data_control_task", 2048 * 2, NULL, 2, &data_task, tskNO_AFFINITY);
	vTaskDelete(NULL);
}

//ds3231
void ds3231_task(void *arg){
	printf("ds3231\n");
	while (1){
		ESP_ERROR_CHECK(ds3231_init_desc(&ds3231, 0, SDA_PIN, SCL_PIN));
		if (xSemaphoreTake(I2C_mutex, portMAX_DELAY) == pdTRUE)
		{
			ESP_LOGI(__func__, "DS3231 take semaphore");
			struct tm time;
			ds3231_get_time(&ds3231, &time);
			printf("%02d/%02d/%02d %02d:%02d:%02d\n", time.tm_year, time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
			data.time_year = time.tm_year;
			data.time_month = time.tm_mon;
			data.time_day = time.tm_mday;
			data.time_hour = time.tm_hour;
			data.time_min = time.tm_min;
			data.time_sec = time.tm_sec;

			ESP_LOGI(__func__, "DS3231 give semaphore");
			ESP_ERROR_CHECK(ds3231_free_desc(&ds3231));
			xSemaphoreGive(I2C_mutex);
			break;
		}
	}
	xTaskCreatePinnedToCore(sht41_task, "sht41_task", 2048 * 2, NULL, 2, &test_sht41, tskNO_AFFINITY);
	vTaskDelete(NULL);
}

//sds011
void sds011_task(void *pvParameters){
	printf("sds\n");

	struct sds011_rx_packet rx_packet;
	while(1)
	{
		sds011_send_cmd_to_queue(&sds011_tx_wakeup_packet, 0);
//		vTaskDelay(pdMS_TO_TICKS(SDS_PERIOD * 60000));
		vTaskDelay(pdMS_TO_TICKS(60000));

		/** Read the data (which is the latest when data queue size is 1). */
		if (sds011_recv_data_from_queue(&rx_packet, 0) == SDS011_OK) {
			ESP_LOGI(__func__, "SDS011 recv data");
		  float pm2_5;
		  float pm10;
		  pm2_5 = ((rx_packet.payload_query_data.pm2_5_high << 8) |
				   rx_packet.payload_query_data.pm2_5_low) /
				  10.0;
		  pm10 = ((rx_packet.payload_query_data.pm10_high << 8) |
				  rx_packet.payload_query_data.pm10_low) /
				 10.0;

		  data.PM2_5 = pm2_5;
		  data.PM10 = pm10;

		  ESP_LOGI(__func__,"PM2.5: %.2f\tPM10: %.2f",pm2_5, pm10);
		  /** Set the sensor to sleep. */
		  sds011_send_cmd_to_queue(&sds011_tx_sleep_packet, 0);
		  ESP_LOGI(__func__, "SDS011 sleep");

//		  xTaskCreatePinnedToCore(ds3231_task, "ds3231_task", 2048 * 2, NULL, 4, &test_ds3231, tskNO_AFFINITY);
		  	//		vTaskDelete(NULL);
		  vTaskDelay(pdMS_TO_TICKS(3 * 60000));
		}
	}
}




void init_app(){
	ESP_ERROR_CHECK( i2cdev_init());
	memset(&dev, 0, sizeof(sht4x_t));
	memset(&ds3231, 0, sizeof(i2c_dev_t));
	sds011_begin(SDS011_UART_PORT, SDS011_TX_GPIO, SDS011_RX_GPIO);
}

void app_main(void)
{




	/*------------------------------------------------------------------*/
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());
    /*------------------------------------------------------------------*/

    I2C_mutex = xSemaphoreCreateMutex();
    sentDataToMQTT_semaphore = xSemaphoreCreateMutex();
    ESP_LOGI(__func__, "Create Semaphore success.");

    dataSensor_queue = xQueueCreate(20, sizeof(data_type));
	data_mqtt_queue = xQueueCreate(20,sizeof(data_type));
	ESP_LOGI(__func__, "Create Queue success.");

    initialize_nvs();
//    wifi_connection();

    init_app();

    xTaskCreatePinnedToCore(sds011_task, "sds011_task", 2048 * 2, NULL, 2, &test_sds011, tskNO_AFFINITY);
}
