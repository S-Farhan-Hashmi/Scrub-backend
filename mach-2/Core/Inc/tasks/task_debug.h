#ifndef TASK_DEBUG_H
#define TASK_DEBUG_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════════════════
 *  GPS + Compass debug state — shared across task_debug, task_gps,
 *  and task_compass.
 *
 *  Defined (storage) in task_debug.c.
 *  Written by task_gps.c and task_compass.c.
 *  Read (display) by DebugTask in task_debug.c.
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    /* sentence accounting */
    uint32_t sentences_rx;      /* every '$' sentence received on DMA */
    uint32_t rmc_parsed;        /* successful $GPRMC / $GNRMC parses  */

    /* fix quality — from $GPGSA / $GNGSA */
    uint8_t  fix_mode;          /* 1 = none, 2 = 2D, 3 = 3D           */
    uint8_t  sats_used;         /* non-empty SV fields in GSA          */
    float    pdop;
    float    hdop;
    float    vdop;

    /* satellite view — from $GPGSV / $GNGSV */
    uint8_t  sats_in_view;      /* field 3 of first GSV sentence       */

    /* timing / status */
    uint32_t last_rmc_tick;     /* xTaskGetTickCount() at last RMC     */
    char     last_status;       /* 'A' (active) or 'V' (void)          */

    /* compass raw (written by task_compass.c) */
    int16_t  cmp_raw_x;
    int16_t  cmp_raw_y;
    int16_t  cmp_raw_z;
    uint32_t cmp_read_errors;
    uint32_t cmp_overflows;
} GPS_Debug_State_t;

/* Storage in task_debug.c — extern for everyone else */
extern GPS_Debug_State_t g_gps_dbg;

/* ── Public API ───────────────────────────────────────────────────── */
void DebugTask(void *arg);
void debug_printf(const char *fmt, ...);

#endif /* TASK_DEBUG_H */
