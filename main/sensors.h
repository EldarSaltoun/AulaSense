// sensors.h — unified sensor API for BME280 + BH1750 + PIR
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize I2C + all sensors (BH1750 + BME280 + PIR).
void sensors_init(void);

// Called periodically to keep readings fresh.
void sensors_sample_tick(void);

// Get latest instantaneous values (no latch here).
// Any of the output pointers may be NULL if not needed.
void sensors_get_latest(float *t_c, float *lux, bool *motion_instant);

// --- Motion latch API ---
// true if motion occurred at any time since last clear()
// Reset after successful upload
bool sensors_get_motion_latched(void);
void sensors_clear_motion_latch(void);

#ifdef __cplusplus
}
#endif
