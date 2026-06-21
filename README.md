                                                                                                        # LocalNodes - Resilient Agricultural Edge Telemetry Firmware

An ESP-IDF firmware application for agricultural sensor nodes with offline-first data storage and automatic reconciliation capabilities. Designed for orchard monitoring with reliable data delivery even during intermittent network connectivity.

## Overview

LocalNodes implements a resilient telemetry system for agricultural field nodes using ESP32 microcontrollers. Each node collects sensor data (soil moisture, water pressure) and transmits it to a Debian gateway server. The system handles network interruptions gracefully by buffering data to flash storage and automatically synchronizing when connectivity is restored.

## Features

- **Offline-First Architecture**: Data is never lost during network outages
- **Automatic Reconciliation**: Backlog synchronization when connection is re-established
- **Ring Buffer Flash Storage**: Efficient circular buffer implementation in SPI flash
- **Wi-Fi Station Mode**: Automatic connection and reconnection to AP
- **FreeRTOS Task Management**: Multi-threaded architecture with state machine
- **11-Byte Packed Records**: Memory-efficient sensor data format

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Sensor Telemetry Node                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────┐    ┌─────────────────┐                   │
│  │  sensor_        │    │  network_       │                   │
│  │  telemetry_     │───►│  state_         │───► [Wi-Fi]       │
│  │  _task          │    │  _fsm           │       │             │
│  └─────────────────┘    └─────────────────┘       ▼             │
│                                  │                 ┌───────────┐ │
│                                  │                 │ Debian    │ │
│                                  └─────────────────┐ │ Gateway   │ │
│                                     (Online)     │ │ Server    │ │
│                                  ┌─────────────────┐ └───────────┘ │
│                                  │                                 │
│                                  │  Flash Queue (local_storage)    │
│                                  │  - Circular buffer              │
│                                  │  - Metadata tracking            │
│                                  │  - Automatic sector erase       │
│                                  └─────────────────┘               │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## State Machine

The network operates in three distinct states:

| State | Behavior |
|-------|----------|
| `SYSTEM_STATE_ONLINE` | Actively streams sensor readings to gateway server |
| `SYSTEM_STATE_OFFLINE` | Buffers data to flash storage, attempts periodic reconnection |
| `SYSTEM_STATE_RECONCILIATION` | Syncs flash backlog to server before resuming live streaming |

## Hardware Requirements

- ESP32 development board
- SPI flash memory (minimum 1MB for local storage partition)
- Wi-Fi connectivity

## Partition Layout

The `partitions.csv` defines the following partitions:

| Partition | Type | Size | Purpose |
|-----------|------|------|---------|
| `nvs` | data/nvs | 24KB | Non-volatile storage for Wi-Fi credentials |
| `phy_init` | data/phy | 4KB | PHY initialization data |
| `factory` | app/factory | 1MB | Main application firmware |
| `local_storage` | data/coredump | 1MB | Sensor data circular buffer |

## Data Format

Each sensor record is packed into 11 bytes:

```c
typedef struct __attribute__((packed)) {
    uint32_t timestamp;     // 4 bytes: Unix epoch time
    uint16_t node_id;       // 2 bytes: Unique node identifier
    float    sensor_reading; // 4 bytes: Soil moisture % or water pressure
    uint8_t  flags;         // 1 byte:  Status flags (0x00=Normal, 0x01=Low Battery)
} sensor_record_t;
```

## Flash Queue Implementation

The flash queue is a ring buffer stored in the `local_storage` partition:

- **Metadata sector**: First 4KB sector holds queue state (head, tail, count, magic token)
- **Data region**: Remaining space stores packed sensor records
- **Sector erasure**: Automatic 4KB sector erase before writing when crossing sector boundaries

### Flash Queue API

```c
// Initialize queue and read existing metadata
esp_err_t flash_queue_init(void);

// Push a new record to the queue
esp_err_t flash_queue_push(const sensor_record_t *record);

// Read multiple records starting from tail
esp_err_t flash_queue_peek_chunk(sensor_record_t *buffer, uint32_t chunk_size, uint32_t offset_from_tail);

// Advance tail pointer after successful transmission
esp_err_t flash_queue_advance_tail(uint32_t count_to_remove);

// Get current queue status
void flash_queue_get_status(uint32_t *count, uint32_t *max_capacity);
```

## Network Protocol

- TCP socket connection to gateway server
- Server IP: `192.16x.xx.xxx` (configurable in `network_state.c`)
- Server Port: `8080`
- Acknowledgment: 1-byte ACK (`0x06`) after each record received

## Building and Flashing

### Prerequisites

- ESP-IDF v5.x installed and configured
- Python 3.7+
- CMake 3.16+

### Build Commands

```bash
# Set target (adjust for your ESP32 variant)
idf.py set-target esp32

# Configure flash partitions (if using custom partition table)
idf.py partition-table-flash

# Build the project
idf.py build

# Flash to device
idf.py -p <PORT> flash monitor
```

## Project Structure

```
Localnodes/
├── CMakeLists.txt           # Root CMake configuration
├── partitions.csv           # Partition table definition
├── main/
│   ├── main.c               # Application entry point and Wi-Fi setup
│   ├── network_state.c      # Network state machine implementation
│   ├── network_state.h      # Network state machine header
│   └── CMakeLists.txt       # Main component configuration
└── components/
    └── flash_queue/
        ├── flash_queue.c      # Flash queue implementation
        ├── flash_queue.h      # Flash queue header
        └── CMakeLists.txt     # Component configuration
```

## Configuration

Wi-Fi credentials can be configured in `main/main.c`:

```c
wifi_config_t wifi_config = {
    .sta = {
        .ssid = "YOUR_SSID",
        .password = "YOUR_PASSWORD",
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        .pmf_cfg = { .capable = true, .required = false },
    },
};
```

