/*
 * task_debug.c
 * Debug output via USART2 (PA2 TX) at 115 200 baud → USB-TTL bridge.
 *
 * debug_printf() is safe to call from any task.
 * Uses xUART2Mutex to serialise access.
 *
 * DebugTask now prints a FULL GPS + Compass diagnostic block every 5 s:
 *
 *   [GPS-DBG] Sentences rx'd: 42  RMC parsed: 10  Fix: NO (V)
 *   [GPS-DBG] Sats in view: 0  Sats used: 0  Fix mode: 1 (No fix)
 *   [GPS-DBG] PDOP: 99.9  HDOP: 99.9  VDOP: 99.9
 *   [GPS-DBG] Last RMC age: 2340 ms
 *   [GPS-DBG] HINT: Module sees 0 sats — check antenna / move outside
 *   [COMPASS-DBG] Chip: QMC5883L (0x0D)  Init: OK
 *   [COMPASS-DBG] Raw X:  1234  Y:  -567  Z:   890
 *   [COMPASS-DBG] Heading: 143.2 deg  Valid: YES
 *   [COMPASS-DBG] Read errors: 0  Overflows: 0
 */

#include "tasks/task_debug.h"
#include "app_types.h"
#include "drivers/compass.h"    /* CompassChip_t, Compass_GetChip() */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/* ── tunables ─────────────────────────────────────────────────────────── */
#define DEBUG_BUF_SIZE   256
#define DEBUG_TX_TIMEOUT 200   /* ms */

/* ── hardware handle ──────────────────────────────────────────────────── */
extern UART_HandleTypeDef huart2;

/* Fix-mode values from $GPGSA field 2 (also used in task_gps.c) */
#define GPS_FIX_NONE   1
#define GPS_FIX_2D     2
#define GPS_FIX_3D     3

/*
 * GPS_Debug_State_t is declared in task_debug.h.
 * This is the one definition (storage) — all other TUs use extern.
 */
GPS_Debug_State_t g_gps_dbg = {
    .fix_mode    = GPS_FIX_NONE,
    .pdop        = 99.9f,
    .hdop        = 99.9f,
    .vdop        = 99.9f,
    .last_status = 'V',
};

/* ── Public printf ───────────────────────────────────────────────────── */
void debug_printf(const char *fmt, ...)
{
    if (xUART2Mutex == NULL) {
        char buf[DEBUG_BUF_SIZE];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        HAL_UART_Transmit(&huart2, (uint8_t *)buf,
                          (uint16_t)strlen(buf), DEBUG_TX_TIMEOUT);
        return;
    }

    if (xSemaphoreTake(xUART2Mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        char buf[DEBUG_BUF_SIZE];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        HAL_UART_Transmit(&huart2, (uint8_t *)buf,
                          (uint16_t)strlen(buf), DEBUG_TX_TIMEOUT);
        xSemaphoreGive(xUART2Mutex);
    }
}

/* ── Helper: fix-mode string ─────────────────────────────────────────── */
static const char *fix_mode_str(uint8_t m)
{
    switch (m) {
        case GPS_FIX_2D: return "2D";
        case GPS_FIX_3D: return "3D";
        default:         return "No fix";
    }
}

/* ── Helper: compass chip string ─────────────────────────────────────── */
static const char *chip_str(CompassChip_t c)
{
    switch (c) {
        case COMPASS_CHIP_HMC5883L: return "HMC5883L (0x1E)";
        case COMPASS_CHIP_QMC5883L: return "QMC5883L (0x0D)";
        default:                    return "UNKNOWN  (not detected)";
    }
}

/* ── Periodic status task ────────────────────────────────────────────── */
void DebugTask(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));

    debug_printf("\r\n");
    debug_printf("============================================\r\n");
    debug_printf("  STM32F446RE + FreeRTOS  —  System Boot   \r\n");
    debug_printf("  Build: " __DATE__ " " __TIME__ "         \r\n");
    debug_printf("============================================\r\n");

    for (;;)
    {
        SensorData_t env_data;

        bool got_sensor = (xQueuePeek(xSensorQueue, &env_data,
                           pdMS_TO_TICKS(1500)) == pdTRUE);

        /* ── Standard system line ── */
        debug_printf("[SYS] Tick:%lu | GPS:%.5f,%.5f(%s) | HDG:%.1f\r\n",
                     (unsigned long)xTaskGetTickCount(),
                     g_gps_lat,
                     g_gps_lon,
                     g_gps_valid ? "OK" : "--",
                     g_heading_deg);

        /* ── Environment ── */
        if (got_sensor) {
            debug_printf(
                "[ENV] Temp:%.1fC | Hum:%.1f%% | MQ135:%.1fppm"
                " | Turb:%.2fNTU | pH:%.2f | TDS:%.1fppm\r\n",
                env_data.temperature,
                env_data.humidity,
                env_data.mq135_ppm,
                env_data.turbidity_ntu,
                env_data.ph,
                env_data.tds_ppm);
        } else {
            debug_printf("[ENV] Waiting for sensor data...\r\n");
        }

        /* ── FlySky ── */
        bool flysky_active = (xTaskGetTickCount() - g_last_flysky_tick)
                              < pdMS_TO_TICKS(500);
        if (flysky_active) {
            debug_printf("[FLYSKY] Throttle:%d | Steering:%d\r\n",
                         g_flysky_throttle, g_flysky_steering);
        } else {
            debug_printf("[FLYSKY] No signal\r\n");
        }

        /* ════════════════════════════════════════════════════════════
         *  GPS FULL DEBUG BLOCK
         * ════════════════════════════════════════════════════════════ */

        /* Snapshot the debug struct under critical section (GPS task
         * writes to it from its own loop, so guard against torn reads). */
        GPS_Debug_State_t snap;
        taskENTER_CRITICAL();
        snap = g_gps_dbg;
        taskEXIT_CRITICAL();

        uint32_t rmc_age = xTaskGetTickCount() - snap.last_rmc_tick;

        debug_printf(
            "[GPS-DBG] Sentences rx: %lu  |  RMC parsed: %lu  |"
            "  Fix: %s (%c)\r\n",
            (unsigned long)snap.sentences_rx,
            (unsigned long)snap.rmc_parsed,
            (snap.last_status == 'A') ? "YES" : "NO",
            snap.last_status);

        debug_printf(
            "[GPS-DBG] Sats in view: %u  |  Sats used: %u  |"
            "  Fix mode: %u (%s)\r\n",
            snap.sats_in_view,
            snap.sats_used,
            snap.fix_mode,
            fix_mode_str(snap.fix_mode));

        debug_printf(
            "[GPS-DBG] PDOP: %.1f  HDOP: %.1f  VDOP: %.1f\r\n",
            snap.pdop, snap.hdop, snap.vdop);

        if (snap.last_rmc_tick == 0) {
            debug_printf("[GPS-DBG] Last RMC: never received\r\n");
        } else {
            debug_printf("[GPS-DBG] Last RMC age: %lu ms\r\n",
                         (unsigned long)rmc_age);
        }

        /* Actionable hint for indoor / acquiring state */
        if (snap.sats_in_view == 0) {
            debug_printf(
                "[GPS-DBG] HINT: No satellites visible."
                " Move near a window or outside.\r\n");
        } else if (snap.fix_mode == GPS_FIX_NONE) {
            debug_printf(
                "[GPS-DBG] HINT: %u sat(s) in view but no fix yet."
                " Keep antenna skyward (cold-start can take 30-90 s).\r\n",
                snap.sats_in_view);
        } else {
            debug_printf(
                "[GPS-DBG] HINT: %u sat(s) used — %s fix active."
                " HDOP %.1f %s\r\n",
                snap.sats_used,
                fix_mode_str(snap.fix_mode),
                snap.hdop,
                (snap.hdop < 2.0f) ? "(excellent)" :
                (snap.hdop < 5.0f) ? "(good)"      :
                (snap.hdop < 10.f) ? "(moderate)"  : "(poor)");
        }

        /* ════════════════════════════════════════════════════════════
         *  COMPASS FULL DEBUG BLOCK
         * ════════════════════════════════════════════════════════════ */

        CompassChip_t chip = Compass_GetChip();

        debug_printf(
            "[COMPASS-DBG] Chip: %s  |  Init: %s\r\n",
            chip_str(chip),
            (chip != COMPASS_CHIP_UNKNOWN) ? "OK" : "FAILED");

        /* Raw magnetometer values (taken from shared debug struct) */
        debug_printf(
            "[COMPASS-DBG] Raw  X: %6d  Y: %6d  Z: %6d\r\n",
            snap.cmp_raw_x,
            snap.cmp_raw_y,
            snap.cmp_raw_z);

        debug_printf(
            "[COMPASS-DBG] Heading: %.1f deg  |  Valid: %s\r\n",
            g_heading_deg,
            (g_heading_deg != 0.0f || snap.cmp_read_errors == 0)
                ? "YES" : "NO");

        debug_printf(
            "[COMPASS-DBG] Read errors: %lu  |  Overflows: %lu\r\n",
            (unsigned long)snap.cmp_read_errors,
            (unsigned long)snap.cmp_overflows);

        if (chip == COMPASS_CHIP_UNKNOWN) {
            debug_printf(
                "[COMPASS-DBG] HINT: Not detected. Check SDA=PB7"
                " SCL=PB8, 4.7k pull-ups to 3.3V, I2C address"
                " 0x0D (QMC) or 0x1E (HMC).\r\n");
        } else if (snap.cmp_overflows > 0) {
            debug_printf(
                "[COMPASS-DBG] HINT: Overflow on X or Y —"
                " possible magnetic interference nearby.\r\n");
        }

        /* ── Heap ── */
        debug_printf("[SYS] FreeHeap:%u bytes\r\n",
                     (unsigned)xPortGetFreeHeapSize());

        debug_printf("--------------------------------------------\r\n");

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
