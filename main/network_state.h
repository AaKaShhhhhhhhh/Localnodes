#ifndef NETWORK_STATE_H
#define NETWORK_STATE_H

#include <stdint.h>

// 1. Define our 3 agricultural system states
typedef enum {
    SYSTEM_STATE_ONLINE,
    SYSTEM_STATE_OFFLINE,
    SYSTEM_STATE_RECONCILIATION
} system_state_t;

// 2. This structure manages our global network engine context
typedef struct {
    system_state_t current_state; // Tracks our active state machine mode
    int server_socket;            // Holds our active Linux server network socket handle
    uint32_t last_ping_time;      // Stores the tick count of our last heartbeat check
} network_context_t;

// Public APIs called by our main loop
void network_state_init(void);
system_state_t get_system_state(void);

#endif // NETWORK_STATE_H