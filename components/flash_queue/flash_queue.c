#include "flash_queue.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "AG_FLASH_LOG";
static const esp_partition_t *s_partition = NULL;
static queue_meta_t s_meta = {0};

#define MAGIC_SIGNATURE 0xDEADBEEF

static esp_err_t save_metadata(void) {
    // Remember Rule #1? We must erase Sector 0 completely before writing new metadata to it
    esp_err_t err = esp_partition_erase_range(s_partition, 0, SECTOR_SIZE);
    if (err != ESP_OK) return err;
    
    // Save the metadata structure to the very beginning of the partition
    return esp_partition_write(s_partition, 0, &s_meta, sizeof(queue_meta_t));
}

esp_err_t flash_queue_init(void) {
    // Find our custom partition we defined in partitions.csv
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION_NAME);
    if (!s_partition) {
        ESP_LOGE(TAG, "Critical Error: partitions.csv is missing '%s'!", PARTITION_NAME);
        return ESP_ERR_NOT_FOUND;
    }
    // Read Sector 0 to see if we have valid coordinates from a previous run
    esp_err_t err = esp_partition_read(s_partition, 0, &s_meta, sizeof(queue_meta_t));
    if (err != ESP_OK) return err;

    // If the flash chip is fresh out of the factory, its memory will be full of 0xFF bytes.
    // Our magic token won't match, meaning we must initialize our system metadata.
    if (s_meta.magic_token != MAGIC_SIGNATURE) {
        ESP_LOGI(TAG, "New Orchard Deployment detected. Formatting storage pointers...");
        s_meta.head_idx = 0;
        s_meta.tail_idx = 0;
        s_meta.count = 0;
        s_meta.magic_token = MAGIC_SIGNATURE;
        err = save_metadata();
        if (err != ESP_OK) return err;
    }

    ESP_LOGI(TAG, "Orchard Storage Online. Backlog: %lu/%d records.", s_meta.count, MAX_RECORDS);
    return ESP_OK;
}

esp_err_t flash_queue_push(const sensor_record_t *record) {
    if (!s_partition) return ESP_ERR_INVALID_STATE;

    // Calculate exactly where this record belongs in the physical hardware memory
    uint32_t write_offset = DATA_START_OFFSET + (s_meta.head_idx * RECORD_SIZE);

    // Tutor Note: Defending against Rule #1! 
    // If our write index lands exactly at the beginning of a fresh 4KB sector, we must wipe it first.
    if (write_offset % SECTOR_SIZE == 0) {
        ESP_LOGI(TAG, "Erasing next raw flash sector block at offset: 0x%lu", write_offset);
        esp_err_t err = esp_partition_erase_range(s_partition, write_offset, SECTOR_SIZE);
        if (err != ESP_OK) return err;
    }

    // Write our 11-byte farm log directly into the raw silicon
    esp_err_t err = esp_partition_write(s_partition, write_offset, record, RECORD_SIZE);
    if (err != ESP_OK) return err;

    // Advance our write pointer to the next slot
    s_meta.head_idx = (s_meta.head_idx + 1) % MAX_RECORDS;

    if (s_meta.count < MAX_RECORDS) {
        s_meta.count++;
    } else {
        // Ring buffer behavior: If the queue is maxed out, we wrap around and advance the tail pointer,
        // dropping the oldest record so the micro-controller never runs out of space.
        s_meta.tail_idx = (s_meta.tail_idx + 1) % MAX_RECORDS;
        ESP_LOGW(TAG, "Orchard Outage severe! Storage full. Overwriting oldest entry.");
    }

    return save_metadata();
}   

esp_err_t flash_queue_peek_chunk(sensor_record_t *buffer, uint32_t chunk_size, uint32_t offset_from_tail) {
    if (!s_partition) return ESP_ERR_INVALID_STATE;
    if (offset_from_tail + chunk_size > s_meta.count) return ESP_ERR_INVALID_ARG;

    uint32_t current_read_idx = (s_meta.tail_idx + offset_from_tail) % MAX_RECORDS;

    // Read consecutive records starting from our oldest unsent data point
    for (uint32_t i = 0; i < chunk_size; i++) {
        uint32_t read_offset = DATA_START_OFFSET + (current_read_idx * RECORD_SIZE);
        esp_err_t err = esp_partition_read(s_partition, read_offset, &buffer[i], RECORD_SIZE);
        if (err != ESP_OK) return err;

        current_read_idx = (current_read_idx + 1) % MAX_RECORDS;
    }

    return ESP_OK;
}

esp_err_t flash_queue_advance_tail(uint32_t count_to_remove) {
    if (!s_partition) return ESP_ERR_INVALID_STATE;
    if (count_to_remove > s_meta.count) return ESP_ERR_INVALID_ARG;

    // Once the Debian server confirms it saved the records, we advance the tail, effectively clearing them
    s_meta.tail_idx = (s_meta.tail_idx + count_to_remove) % MAX_RECORDS;
    s_meta.count -= count_to_remove;

    return save_metadata();
}

void flash_queue_get_status(uint32_t *count, uint32_t *max_capacity) {
    if (count) *count = s_meta.count;
    if (max_capacity) *max_capacity = MAX_RECORDS;
}