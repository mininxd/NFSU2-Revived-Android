#ifndef ROAM_NITRO_FIX_H
#define ROAM_NITRO_FIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * roam_nitro_fix.h - Dedicated Vehicle Speed & Nitrous Telemetry Subsystem for NFSU2 Android.
 * Provides resilient, view-independent player vehicle telemetry and auto-gas speed threshold evaluation.
 */
float roam_nitro_get_car_speed_kmh(void);
int   roam_nitro_get_car_nos_percent(void);
int   roam_nitro_is_nos_active(void);

#ifdef __cplusplus
}
#endif

#endif /* ROAM_NITRO_FIX_H */
