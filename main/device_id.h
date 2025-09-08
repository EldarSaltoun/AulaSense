// main/device_id.h
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ======== Device identity (custom, not MAC) ========
// Set your defaults here. You can change and rebuild, or later implement NVS setters.
#ifndef DEVICE_BUILDING_DEFAULT
#define DEVICE_BUILDING_DEFAULT "Ficus"
#endif
#ifndef DEVICE_NUMBER_DEFAULT
#define DEVICE_NUMBER_DEFAULT   "101"
#endif

typedef struct {
    char building[16];
    char number[16];
} device_id_t;

// Fill out with compile-time defaults (or from NVS in the future)
static inline void device_id_get(device_id_t *out)
{
    if (!out) return;
    const char *b = DEVICE_BUILDING_DEFAULT;
    const char *n = DEVICE_NUMBER_DEFAULT;

    size_t i=0;
    for (; b[i] && i < sizeof(out->building)-1; ++i) out->building[i] = b[i];
    out->building[i] = '\0';

    i=0;
    for (; n[i] && i < sizeof(out->number)-1; ++i) out->number[i] = n[i];
    out->number[i] = '\0';
}

#ifdef __cplusplus
}
#endif
