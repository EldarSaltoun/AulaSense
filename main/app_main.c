// main/app_main.c — 10Hz sampling, 1Hz raw logging, 10s publish, 10s send
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "sensors.h"
#include "time_sync.h"
#include "wifi.h"
#include "uploader.h"
#include "device_id.h"

#include <time.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "APP";

// --------- tasks ---------

// Keep sensor values fresh (BH1750/BME280/PIR) at 10 Hz
static void sampler_task(void *pv) {
    (void)pv;
    int log_ctr = 0;
    while (1) {
        sensors_sample_tick();   // updates internal cache

        // Print once per second (every 10th sample)
        if (++log_ctr >= 10) {
            float t = 0.0f, lux = 0.0f;
            bool m = false;
            sensors_get_latest(&t, &lux, &m);
            ESP_LOGI("SAMPLER", "Raw: Temp=%.2fC Lux=%.1f Motion=%s",
                     t, lux, m ? "true" : "false");
            log_ctr = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));   // every 100 ms → 10 Hz
    }
}

// Build one sample every 10 s, print locally (with local time), and buffer it
static void publisher_task(void *pv) {
    (void)pv;

    device_id_t id;
    device_id_get(&id); // fills building/number

    while (1) {
        float t_c = 0.0f, lux = 0.0f;
        bool motion_inst = false;
        bool motion_lat  = sensors_get_motion_latched();

        sensors_get_latest(&t_c, &lux, &motion_inst);

        // Local timestamp (IST/IDT) — SNTP + TZ handled in time_sync_start()
        char ts_local[32];
        time_sync_fmt(ts_local, sizeof(ts_local));

        ESP_LOGI(TAG,
                 "[%s] Temp=%.2fC Lux=%.1f Motion(inst)=%s Motion(latched)=%s %s/%s",
                 ts_local,
                 t_c, lux,
                 motion_inst ? "true" : "false",
                 motion_lat  ? "true" : "false",
                 id.building, id.number);

        time_t now = 0;
        time(&now);
        struct tm tm_local = {0};
        localtime_r(&now, &tm_local);

        sample_t s = {0};
        s.tm_local = tm_local;
        s.temp_c   = t_c;
        s.lux      = lux;
        s.motion   = (motion_inst || motion_lat);

        strncpy(s.building, id.building, sizeof(s.building) - 1);
        strncpy(s.number,   id.number,   sizeof(s.number)   - 1);

        if (!uploader_add(&s)) {
            ESP_LOGW(TAG, "Uploader buffer full — sample dropped");
        }

        sensors_clear_motion_latch();
        vTaskDelay(pdMS_TO_TICKS(10000));   // every 10s
    }
}

// Push buffered samples via HTTPS every 10 s (and log exact JSON)
static void sender_task(void *pv) {
    (void)pv;
    uploader_set_log_json(true);
    while (1) {
        uploader_send();
        vTaskDelay(pdMS_TO_TICKS(10000));   // every 10s
    }
}

// --------- entry ---------
void app_main(void) {
    ESP_LOGI(TAG, "App starting...");
    ESP_ERROR_CHECK(nvs_flash_init());

    // Wi-Fi: try closed SSID first, fallback to open scan
    wifi_init_prefer_closed();

    time_sync_start();
    sensors_init();
    uploader_init("https://aulasense.onrender.com/sensors/upload");

    xTaskCreate(sampler_task,   "sampler_task",   4096, NULL, 5, NULL);
    xTaskCreate(publisher_task, "publisher_task", 4096, NULL, 5, NULL);
    xTaskCreate(sender_task,    "sender_task",    4096, NULL, 5, NULL);
}
