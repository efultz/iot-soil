/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mqtt_client.h"

#include "u8g2.h"
#include "u8g2_esp32_hal.h"



extern const uint8_t server_cert_pem_start[] asm("_binary_AmazonRootCA1_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_AmazonRootCA1_pem_end");
extern const uint8_t client_cert_pem_start[] asm("_binary_soil_03_esp32_cert_pem_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_soil_03_esp32_cert_pem_end");
extern const uint8_t client_key_pem_start[] asm("_binary_soil_03_esp32_private_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_soil_03_esp32_private_key_end");

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "productOps-2.4"
#define EXAMPLE_ESP_WIFI_PASS      "innovate"
#define EXAMPLE_ESP_MAXIMUM_RETRY  3

// #if CONFIG_ESP_WIFI_AUTH_OPEN
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
// #elif CONFIG_ESP_WIFI_AUTH_WEP
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
// #elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
// #endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "esp32_proj1";

static int s_retry_num = 0;


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "$aws/things/soil_03_esp32/shadow/update/delta", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "$aws/things/soil_03_esp32/shadow/update", "{\"state\": {\"reported\": {\"data\": \"red\"}}}", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	     * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);

        
        // esp_tls_cfg_t cfg = {
        //     .crt_bundle_attach = esp_crt_bundle_attach,
        // };

        // esp_tls_t *tls = esp_tls_init();
        // if (!tls) {
        //     ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
        // }

        // const char *hostname = "a3bjug9t7i1w5h-ats.iot.us-west-2.amazonaws.com";
        // if (esp_tls_conn_new_sync(hostname, strlen(hostname), 443, &cfg, tls) == 1) {
        //     ESP_LOGI(TAG, "Connection established to %s", "awsiot");
        //     // conn_count++;
        // } else {
        //     ESP_LOGE(TAG, "Could not connect to %s", "awsiot");
        // }
        // esp_tls_conn_destroy(tls);
        const char * amznalpnproto[] = { "x-amzn-mqtt-ca", 0 };
        const esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = "mqtts://a3bjug9t7i1w5h-ats.iot.us-west-2.amazonaws.com:443",
            .broker.verification = {
                .certificate = (const char *)server_cert_pem_start,
                .alpn_protos = amznalpnproto,
            },
            .credentials = {
                .client_id = "soil_03_esp32",
                .authentication = {
                    .certificate = (const char *)client_cert_pem_start,
                    .key = (const char *)client_key_pem_start,
                },
            },
        };

        ESP_LOGI(TAG, "Free memory: %ld bytes", esp_get_free_heap_size());
        esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
        /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
        esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(client);


        
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}


// Create a U8g2log object
u8log_t u8log;

// assume 4x6 font, define width and height
#define U8LOG_WIDTH 16
#define U8LOG_HEIGHT 4

// allocate memory
uint8_t u8log_buffer[U8LOG_WIDTH*U8LOG_HEIGHT];

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    // esp_log_level_set("mqtt_client",  ESP_LOG_DEBUG);
    // esp_log_level_set("esp-tls-mbedtls",  ESP_LOG_DEBUG);
    // esp_log_level_set("transport_base",  ESP_LOG_DEBUG);
    esp_log_level_set("u8g2_hal",  ESP_LOG_DEBUG);
    esp_log_level_set("i2c",  ESP_LOG_DEBUG);
    esp_log_level_set("esp32_proj1",  ESP_LOG_DEBUG);
    
    


    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = GPIO_NUM_5;
    u8g2_esp32_hal.bus.i2c.scl = GPIO_NUM_4;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_t u8g2; // a structure which will contain all the data for one display
    
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
      &u8g2, U8G2_R2,
      // u8x8_byte_sw_i2c,
      u8g2_esp32_i2c_byte_cb,
      u8g2_esp32_gpio_and_delay_cb); // init u8g2 structure
    ESP_LOGI(TAG, "finished u8g2 setup");


    u8log_Init(&u8log, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer);
    ESP_LOGI(TAG, "finished u8log init");

    u8log_SetCallback(&u8log, u8log_u8g2_cb, &u8g2); 
    ESP_LOGI(TAG, "finished u8log setcallback");

    u8x8_SetI2CAddress(&u8g2.u8x8, 0x3c << 1);
    ESP_LOGI(TAG, "set u8g2 address");

    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,
    ESP_LOGI(TAG, "finished u8g2 init");

    ESP_LOGI(TAG, "u8g2_SetPowerSave");
    u8g2_SetPowerSave(&u8g2, 0);  // wake up display
    ESP_LOGI(TAG, "u8g2_ClearBuffer");
    u8g2_ClearBuffer(&u8g2);
    // ESP_LOGI(TAG, "u8g2_DrawBox");
    // u8g2_DrawBox(&u8g2, 0, 26, 80, 6);
    // u8g2_DrawFrame(&u8g2, 0, 26, 100, 6);


// TINY:
    // u8g2_SetFont(&u8g2, u8g2_font_simple1_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_minuteconsole_tr);


// SMALL:
    // u8g2_SetFont(&u8g2, u8g2_font_helvR08_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_glasstown_nbp_tr);

    // u8g2_SetFont(&u8g2, u8g2_font_prospero_nbp_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_crox1h_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_minicute_tr);

// MEDIUM:
    // u8g2_SetFont(&u8g2, u8g2_font_helvR10_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_luRS10_tr);

// MEDIUMLARGE:
    // u8g2_SetFont(&u8g2, u8g2_font_helvR12_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_crox3h_tr);


// smallcaps:
    // u8g2_SetFont(&u8g2, u8g2_font_mercutio_sc_nbp_tr);

// ok, not good enough
    // u8g2_SetFont(&u8g2, u8g2_font_crox2h_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_Untitled16PixelSansSerifBitmap_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_HelvetiPixel_tr);
    // u8g2_SetFont(&u8g2, u8g2_font_tallpixelextended_tr);

    ESP_LOGI(TAG, "u8g2_SetFont");

    u8g2_SetFont(&u8g2, u8g2_font_helvR10_tr);

    ESP_LOGI(TAG, "u8g2_DrawStr");
    // u8g2_DrawStr(&u8g2, 2, 17, "Hi nkolban!");
    u8g2_DrawStr(&u8g2, 2, 15, "ETAOINSHRDL");
    u8g2_DrawStr(&u8g2, 2, 32, "etaoinshrdlcumw");

    ESP_LOGI(TAG, "u8g2_SendBuffer");
    u8g2_SendBuffer(&u8g2);

    ESP_LOGI(TAG, "All done!");
    vTaskDelay( 2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "write line");

    u8log_WriteString(&u8log, "HEADER 1:\n");
    ESP_LOGI(TAG, "wrote 1");
    vTaskDelay( 2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "write line");
    u8log_WriteString(&u8log, "log line 1\n");
    ESP_LOGI(TAG, "wrote 2");


    // wifi_init_sta();
}
