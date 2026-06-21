#include "network_state.h"
#include "flash_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"  // Low-Level standard TCP socket library

static const char *TAG = "AG_NET_FSM";
network_context_t net_ctx = {0};

#define SERVER_IP   "192.168.29.143"
#define SERVER_PORT 8080

// Internal helper: Safely attempt connection to our Debian gateway server
static bool connect_to_gateway(void) {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) return false;

    // Set a timeout of 2 seconds so we don't block execution endlessly if server is dead
    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        close(sock);
        return false;
    }

    net_ctx.server_socket = sock;
    return true;
}

// Background FreeRTOS Execution Engine
static void network_sm_task(void *pvParameters) {
    net_ctx.current_state = SYSTEM_STATE_OFFLINE; // Start default offline until proven otherwise

    while (1) {
        uint32_t backlog_count = 0;
        flash_queue_get_status(&backlog_count, NULL);

        switch (net_ctx.current_state) {
            
            case SYSTEM_STATE_OFFLINE:
                ESP_LOGI(TAG, "State: OFFLINE. Pinging farm gateway server...");
                if (connect_to_gateway()) {
                    if (backlog_count > 0) {
                        ESP_LOGW(TAG, "Gateway found! Local backlog of %lu entries detected. Switching to RECONCILIATION mode.", backlog_count);
                        net_ctx.current_state = SYSTEM_STATE_RECONCILIATION;
                    } else {
                        ESP_LOGI(TAG, "Gateway found! Clear skies. Switching to ONLINE live mode.");
                        net_ctx.current_state = SYSTEM_STATE_ONLINE;
                    }
                } else {
                    ESP_LOGE(TAG, "Gateway unreachable. Retrying link in 5 seconds...");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
                break;

            case SYSTEM_STATE_ONLINE:
                // If we are online but a flash backlog suddenly exists, shift to clear it out
                if (backlog_count > 0) {
                    net_ctx.current_state = SYSTEM_STATE_RECONCILIATION;
                }
                vTaskDelay(pdMS_TO_TICKS(1000)); 
                break;

            case SYSTEM_STATE_RECONCILIATION:
                ESP_LOGI(TAG, "State: RECONCILIATION. Clearing backlog tracking queue...");

                while (backlog_count > 0) {
                sensor_record_t record_to_send;
        
        // 1. Peek at the single oldest record without deleting it yet
                 esp_err_t err = flash_queue_peek_chunk(&record_to_send, 1, 0);
                 if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to read flash backlog.");
                 break;
        }

        // 2. Transmit that 11-byte packed struct down the socket line
        int bytes_sent = send(net_ctx.server_socket, &record_to_send, sizeof(sensor_record_t), 0);
        if (bytes_sent < 0) {
            ESP_LOGE(TAG, "Network link shattered mid-transfer! Reverting to offline mode.");
            net_ctx.current_state = SYSTEM_STATE_OFFLINE;
            close(net_ctx.server_socket);
            break;
        }

        // 3. Wait for the server to reply with the 1-byte ACK token
        uint8_t ack_response = 0;
        int bytes_received = recv(net_ctx.server_socket, &ack_response, 1, 0);
        
        // If the server went silent or disconnected, abort!
        if (bytes_received <= 0 || ack_response != 0x06) {
            ESP_LOGE(TAG, "Server failed to acknowledge record receipt! Dropping link.");
            net_ctx.current_state = SYSTEM_STATE_OFFLINE;
            close(net_ctx.server_socket);
            break;
        }

        // 4. Success! Server confirmed save. Safely clear it from flash by pushing tail forward
        flash_queue_advance_tail(1);
        
        // Refresh our count variable for the loop check
        flash_queue_get_status(&backlog_count, NULL);
    }

    if (backlog_count == 0) {
        ESP_LOGI(TAG, "All data synced successfully! Entering ONLINE streaming mode.");
        net_ctx.current_state = SYSTEM_STATE_ONLINE;
    }
    break;
        }
    }
}

void network_state_init(void) {
    // Spin up our network engine inside an isolated, low-priority FreeRTOS worker thread
    xTaskCreatePinnedToCore(network_sm_task, "net_sm_task", 4096, NULL, 5, NULL, 1);
}

system_state_t get_system_state(void) {
    return net_ctx.current_state;
}