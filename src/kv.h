#ifndef KV_H
#define KV_H

#include <stdint.h>

typedef enum
{
    SYNC_NONE,
    SYNC_ALWAYS,
    SYNC_INTERVAL
} sync_mode_t;

int bootup(sync_mode_t sync_mode, int interval_ms);

// whether a write at time `now` (ms) should be followed by a sync; exposed for tests
int should_sync(sync_mode_t mode, uint64_t now, uint64_t last_sync_time, uint64_t interval);
int put(char *key, char *value);
int get(char *key, char **value);
int delete (char *key);
void shutdown();
void reset();

#endif