#include "time_sync.h"
#include "esp_log.h"

#include "lwip/apps/sntp.h"   // LWIP SNTP (works across IDF 5.x)
#include <time.h>
#include <string.h>
#include <stdlib.h>   // for setenv()

static const char *TAG = "TIME_SYNC";
static bool s_started = false;

void time_sync_start(void)
{
    if (s_started) return;

    // Configure SNTP in polling mode with multiple servers
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.google.com");
    sntp_setservername(2, "time.windows.com");

    // Start SNTP (non-blocking; time will update asynchronously)
    sntp_init();
    s_started = true;
    ESP_LOGI(TAG, "SNTP started via LWIP");

    // --- Set timezone to Israel (IST/IDT) ---
    // "IST-2IDT,M3.4.4/26,M10.5.0"
    //   -> base UTC+2
    //   -> switch to DST (IDT, UTC+3) on last Friday in March at 02:00
    //   -> back to standard time last Sunday in October at 02:00
    setenv("TZ", "IST-2IDT,M3.4.4/26,M10.5.0", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to Israel (IST/IDT)");
}

void time_sync_fmt(char *out, size_t out_len)
{
    if (!out || out_len == 0) return;

    time_t now = 0;
    time(&now);

    // If not yet synced, time_t may be near 0
    if (now < 1700000000) { // ~2023-11-14 epoch; simple "not synced yet" heuristic
        snprintf(out, out_len, "UNSYNCED");
        return;
    }

    struct tm tm_local;
    localtime_r(&now, &tm_local);  // <-- use local time (IST/IDT)
    strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &tm_local);
}
