#pragma once
#include <stdbool.h>
#include <stddef.h>   // <-- needed for size_t

#ifdef __cplusplus
extern "C" {
#endif

void time_sync_start(void);                    // start SNTP (non-blocking)
void time_sync_fmt(char *buf, size_t n);       // "YYYY-MM-DD HH:MM:SS" UTC or "UNSYNCED"

#ifdef __cplusplus
}
#endif
