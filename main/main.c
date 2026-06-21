#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "flash_queue.h"
#include "network_state.h"
#include "lwip/sockets.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_random.h"

//  ADD THESE TWO PROTOTYPES HERE TO FIX THE COMPILER ERROR:
extern esp_err_t flash_queue_init(void); 
extern void network_state_init(void);

// Distinct tags for clean log filtering
static const char *WIFI_TAG = "WIFI_SETUP";
static const char *APP_TAG  = "MAIN_APP";

// --- Wi-Fi Event Handler ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(WIFI_TAG, "Connecting to AP...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(WIFI_TAG, "Disconnected from AP. Retrying connection...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "Successfully Connected! Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// --- Initialize Wi-Fi Station Mode ---
void initialise_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // 6. Set credentials and mode with structural safety parameters
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "xxxxx",
            .password = "xxxxx",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Explicitly tell the chip to use standard WPA2
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(WIFI_TAG, "Wi-Fi Station storage structures initialized.");
}

// --- Simulated Agricultural Sensor Task ---
extern network_context_t net_ctx; // Tells main.c this lives in another file
void sensor_telemetry_task(void *pvParameters) {
    uint32_t mock_timestamp = 1781643600; // Mock Epoch Time Start
    uint16_t factory_node_id = 101;       // Almond Orchard Block North Node

    while (1) {
        sensor_record_t current_reading = {
            .timestamp = mock_timestamp,
            .node_id = factory_node_id,
            .sensor_reading = 35.5f + ((float)(esp_random() % 100) / 20.0f),
            .flags = 0x00
        };

        system_state_t live_state = get_system_state();
        
        if (live_state == SYSTEM_STATE_ONLINE) {
            ESP_LOGI(APP_TAG, " [ONLINE MODE] Direct-streaming reading: %.2f%% to Debian gateway...", current_reading.sensor_reading);
            
            //  FIX: Actually push the struct down the active socket pipeline!
            int bytes_sent = send(net_ctx.server_socket, &current_reading, sizeof(sensor_record_t), 0);
            
            // Handle mid-stream network drops gracefully
            if (bytes_sent < 0) {
                ESP_LOGE(APP_TAG, "Transmission broke during live streaming! Falling back to offline recovery.");
                // Replace set_system_state(SYSTEM_STATE_OFFLINE); with:
                net_ctx.current_state = SYSTEM_STATE_OFFLINE;        
                close(net_ctx.server_socket);
            }
        }
        else if (live_state == SYSTEM_STATE_OFFLINE) {
            ESP_LOGW(APP_TAG, " [OFFLINE MODE] Network severed! Buffering data point safely to local flash...");
            
            esp_err_t err = flash_queue_push(&current_reading);
            if (err != ESP_OK) {
                ESP_LOGE(APP_TAG, "Critical flash write error encountered!");
            }
        } 
        else if (live_state == SYSTEM_STATE_RECONCILIATION) {
            ESP_LOGI(APP_TAG, "[SYNC MODE] Telemetry loop waiting for flash backlog clearance handshake...");
        }

        mock_timestamp += 2; 
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// --- System Core Entry ---
void app_main(void) {
    ESP_LOGI(APP_TAG, "Commencing Resilient Local-First Edge Node Firmware Application...");

    // 1. Boot native system storage parameters (Required by Wi-Fi driver)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Mount local custom flash logging partition
    flash_queue_init(); 

    // 3. Fire up the Wi-Fi client connection configuration
    initialise_wifi(); 

    // 4. Safety buffer for background link layer threads to spin up safely
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 5. Start your network background state machine loop
    network_state_init(); 
    
    // 6. Spawn the data recording task
    xTaskCreate(sensor_telemetry_task, "sensor_telemetry_task", 4096, NULL, 5, NULL);
}