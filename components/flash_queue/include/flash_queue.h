#ifndef FLASH_QUEUE_H
#define FLASH_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define PARTITION_NAME      "local_storage"
#define SECTOR_SIZE         4096
#define RECORD_SIZE         11   // Explicitly 11 bytes packed!

#define DATA_START_OFFSET   SECTOR_SIZE // Leave the first 4KB sector for tracking metadata
#define TOTAL_PARTITION_SZ  (1024 * 1024)
#define MAX_RECORDS         ((TOTAL_PARTITION_SZ - DATA_START_OFFSET) / RECORD_SIZE)

// This is our precise 11-byte agricultural payload
typedef struct __attribute__((packed)) {
    uint32_t timestamp;     // 4 bytes: Epoch time (When was this measured?)
    uint16_t node_id;       // 2 bytes: Unique ID of this specific field node
    float sensor_reading;   // 4 bytes: Soil Moisture % or Water Pressure
    uint8_t flags;          // 1 byte:  0x00=Normal, 0x01=Low Solar Battery Warning
} sensor_record_t;          // Total = 11 bytes

// This keeps track of where we are in our flash notebook
typedef struct __attribute__((packed)) {
    uint32_t head_idx;      // Where to write next
    uint32_t tail_idx;      // Where the oldest unsent record is
    uint32_t count;         // Current total backlog of records
    uint32_t magic_token;   // A unique signature (0xDEADBEEF) to know if flash was initialized
} queue_meta_t;

// Master functions our main program will call
esp_err_t flash_queue_init(void);
esp_err_t flash_queue_push(const sensor_record_t *record);
esp_err_t flash_queue_peek_chunk(sensor_record_t *buffer, uint32_t chunk_size, uint32_t offset_from_tail);
esp_err_t flash_queue_advance_tail(uint32_t count_to_remove);
void flash_queue_get_status(uint32_t *count, uint32_t *max_capacity);

#endif