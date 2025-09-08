// main/uploader.h
#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct tm tm_local;    // date/time split at send time
    float temp_c;
    float lux;
    bool motion;
    char building[16];
    char number[16];
} sample_t;

void      uploader_init(const char *url);
bool      uploader_add(const sample_t *s);  // returns false if buffer full
esp_err_t uploader_send(void);              // POST accumulated samples as JSON array
int       uploader_count(void);             // how many pending

// Enable/disable echoing the JSON payload to UART logs before POSTing
void      uploader_set_log_json(bool enable);

#ifdef __cplusplus
}
#endif
