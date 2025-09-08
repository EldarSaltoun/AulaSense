// main/uploader.c
#include "uploader.h"
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#ifndef UPLOADER_MAX_SAMPLES
#define UPLOADER_MAX_SAMPLES 20
#endif

static const char *TAG = "UPLOADER";
static sample_t s_buf[UPLOADER_MAX_SAMPLES];
static int s_count = 0;
static char s_url[128] = {0};
static bool s_log_json = false;

void uploader_set_log_json(bool enable) { s_log_json = enable; }

void uploader_init(const char *url)
{
    if (url) {
        size_t n = strlen(url);
        if (n >= sizeof(s_url)) n = sizeof(s_url)-1;
        memcpy(s_url, url, n);
        s_url[n] = '\0';
    }
}

bool uploader_add(const sample_t *s)
{
    if (!s || s_count >= UPLOADER_MAX_SAMPLES) return false;
    s_buf[s_count++] = *s;
    return true;
}

int uploader_count(void) { return s_count; }

static cJSON* build_payload(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;

    for (int i = 0; i < s_count; ++i) {
        const sample_t *p = &s_buf[i];
        char date_str[16];   // YYYY-MM-DD
        char time_str[16];   // HH:MM:SS
        strftime(date_str, sizeof(date_str), "%Y-%m-%d", &p->tm_local);
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &p->tm_local);

        cJSON *obj = cJSON_CreateObject();
        if (!obj) { cJSON_Delete(arr); return NULL; }

        cJSON_AddStringToObject(obj, "date", date_str);
        cJSON_AddStringToObject(obj, "time", time_str);
        cJSON_AddNumberToObject(obj, "temp", p->temp_c);
        cJSON_AddNumberToObject(obj, "lux",  p->lux);
        cJSON_AddBoolToObject  (obj, "motion", p->motion);
        cJSON_AddStringToObject(obj, "building", p->building);
        cJSON_AddStringToObject(obj, "number",   p->number);

        cJSON_AddItemToArray(arr, obj);
    }
    return arr;
}

esp_err_t uploader_send(void)
{
    if (s_count == 0) return ESP_OK;
    if (s_url[0] == '\0') {
        ESP_LOGE(TAG, "No URL set");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;
    cJSON *arr = build_payload();
    if (!arr) return ESP_ERR_NO_MEM;

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) return ESP_ERR_NO_MEM;

    if (s_log_json) {
        // Show exactly what will be sent
        ESP_LOGI(TAG, "Preparing to POST %d sample(s) to %s", s_count, s_url);
        ESP_LOGI(TAG, "JSON payload: %s", json);
    } else {
        ESP_LOGI(TAG, "Preparing to POST %d sample(s) to %s", s_count, s_url);
    }

    esp_http_client_config_t cfg = {
        .url = s_url,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach, // use built-in CA bundle
        .skip_cert_common_name_check = false,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) { free(json); return ESP_ERR_NO_MEM; }

    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_post_field(h, json, strlen(json));

    err = esp_http_client_perform(h);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(h);
        int len    = esp_http_client_get_content_length(h);
        ESP_LOGI(TAG, "HTTP status: %d, content-length: %d", status, len);
        if (status >= 200 && status < 300) {
            ESP_LOGI(TAG, "Upload OK — clearing %d buffered sample(s)", s_count);
            s_count = 0; // clear buffer on success
        } else {
            ESP_LOGW(TAG, "Upload failed (status %d) — keeping %d sample(s) buffered", status, s_count);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "POST failed: %s — keeping %d sample(s) buffered",
                 esp_err_to_name(err), s_count);
    }

    esp_http_client_cleanup(h);
    free(json);
    return err;
}
