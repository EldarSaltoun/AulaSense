#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WIFI_AUTO";

// Event group bits
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// --- Configure your closed Wi-Fi credentials here ---
#define WIFI_CLOSED_SSID "limor22"
#define WIFI_CLOSED_PASS "26051960"

// Forward declaration
static esp_err_t try_connect_closed(const char *ssid, const char *pass);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA start → scanning for open networks...");
        esp_wifi_scan_start(NULL, false);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);

        wifi_ap_record_t *ap_records = malloc(ap_num * sizeof(wifi_ap_record_t));
        if (!ap_records) {
            ESP_LOGE(TAG, "Failed to allocate memory for AP list");
            return;
        }

        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

        int best_rssi = -127;
        int best_index = -1;

        for (int i = 0; i < ap_num; i++) {
            if (ap_records[i].authmode == WIFI_AUTH_OPEN) {
                ESP_LOGI(TAG, "Found OPEN SSID='%s' RSSI=%d",
                         ap_records[i].ssid, ap_records[i].rssi);
                if (ap_records[i].rssi > best_rssi) {
                    best_rssi = ap_records[i].rssi;
                    best_index = i;
                }
            }
        }

        if (best_index >= 0) {
            wifi_config_t wifi_config = {0};
            strncpy((char *)wifi_config.sta.ssid,
                    (char *)ap_records[best_index].ssid,
                    sizeof(wifi_config.sta.ssid));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_connect());

            ESP_LOGI(TAG, "Connecting to OPEN SSID='%s' RSSI=%d",
                     ap_records[best_index].ssid, ap_records[best_index].rssi);
        } else {
            ESP_LOGW(TAG, "No open networks found. Will retry in 5s...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_wifi_scan_start(NULL, false);
        }

        free(ap_records);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected to AP");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected → retrying scan...");
        esp_wifi_scan_start(NULL, false);
    }
}

// --- Closed Wi-Fi attempt ---
static esp_err_t try_connect_closed(const char *ssid, const char *pass) {
    ESP_LOGI(TAG, "Attempting CLOSED WiFi: SSID='%s'", ssid);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait max **20s** for connection (to avoid false reset)
    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(20000)   // extended wait from 10s → 20s
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ Connected via CLOSED WiFi: '%s'", ssid);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "❌ Failed to connect CLOSED WiFi: '%s'", ssid);
        return ESP_FAIL;
    }
}

// --- Common Wi-Fi initialization (only once) ---
static void wifi_common_init(void) {
    static bool initialized = false;
    if (initialized) return;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_event_group = xEventGroupCreate();
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
    initialized = true;
}

// --- Open Wi-Fi auto-scan ---
void wifi_init_auto(void) {
    ESP_LOGI(TAG, "Initializing WiFi in auto-open mode...");
    wifi_common_init();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_scan_start(NULL, false);
}

// --- Closed-first entry point ---
void wifi_init_prefer_closed(void) {
    ESP_LOGI(TAG, "Initializing WiFi (closed-first fallback)...");
    wifi_common_init();

    if (try_connect_closed(WIFI_CLOSED_SSID, WIFI_CLOSED_PASS) != ESP_OK) {
        ESP_LOGW(TAG, "Falling back to OPEN WiFi auto-connect...");
        wifi_init_auto();
    }
}
