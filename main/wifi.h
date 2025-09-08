#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi in auto-open mode.
 *
 * This function sets up WiFi as a station, scans for available networks,
 * and connects automatically to the strongest **open (unencrypted)** network.
 * 
 * The connection runs in the background. Logs will indicate the SSID and status.
 */
void wifi_init_auto(void);

/**
 * @brief Initialize WiFi, trying closed WiFi first, then falling back to open.
 *
 * Attempts to connect to the configured closed Wi-Fi SSID with password.
 * If it fails, automatically falls back to scanning for open networks.
 *
 * Waits up to 20 seconds for a closed connection before failing.
 */
void wifi_init_prefer_closed(void);

#ifdef __cplusplus
}
#endif
