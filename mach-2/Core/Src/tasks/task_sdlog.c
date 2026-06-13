/*
 * task_sdlog.c
 * Logs SensorData_t to a CSV file on the SD card via FatFS.
 *
 * File naming: LOG_<tick_at_boot>.CSV
 * Format: tick,temp,hum,mq135,turbidity,ph,tds,lat,lon,heading,gps_ok,dht_ok
 *
 * Write strategy:
 *   - f_open with FA_OPEN_APPEND each write (safe but slower)
 *   - f_sync after every write for power-loss safety
 *   - If SD mount fails, task retries every 10s and logs to debug
 *
 * SPI1 is used for SD. FatFS FS_REENTRANT must be 1 in ffconf.h
 * (uses xSDMutex via ffsystem.c).
 */
#include "tasks/task_debug.h"
#include "tasks/task_sdlog.h"
#include "app_types.h"
#include "fatfs.h"
#include <stdio.h>
#include <string.h>

#define SD_RETRY_DELAY_MS   10000
#define CSV_HEADER  "tick,temp_c,hum_pct,mq135_ppm,turbidity_ntu," \
                    "ph,tds_ppm,latitude,longitude,heading_deg,gps_ok,dht_ok\n"

static FATFS fs;
static FIL   file;
static char  fname[32];
static bool  mounted = false;

/* ── Mount with retry ────────────────────────────────────────────────── */
static bool sd_mount(void)
{
    FRESULT fr;
    if (xSemaphoreTake(xSDMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        fr = f_mount(&fs, "", 1);
        xSemaphoreGive(xSDMutex);
        if (fr == FR_OK) return true;
        debug_printf("[SD] Mount failed: %d\r\n", (int)fr);
    }
    return false;
}

/* ── Write CSV header ────────────────────────────────────────────────── */
static bool sd_write_header(void)
{
    FRESULT fr;
    if (xSemaphoreTake(xSDMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        fr = f_open(&file, fname, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr == FR_OK) {
            f_puts(CSV_HEADER, &file);
            f_sync(&file);
            f_close(&file);
        }
        xSemaphoreGive(xSDMutex);
        return (fr == FR_OK);
    }
    return false;
}

/* ── Append one data row ─────────────────────────────────────────────── */
static bool sd_append(const char *row)
{
    FRESULT fr;
    bool ok = false;

    if (xSemaphoreTake(xSDMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        fr = f_open(&file, fname, FA_OPEN_APPEND | FA_WRITE);
        if (fr == FR_OK) {
            f_puts(row, &file);
            f_sync(&file);
            f_close(&file);
            ok = true;
        } else {
            debug_printf("[SD] f_open append failed: %d\r\n", (int)fr);
        }
        xSemaphoreGive(xSDMutex);
    }
    return ok;
}

/* ── Task ────────────────────────────────────────────────────────────── */
void SDLogTask(void *arg)
{
    (void)arg;

    /* Wait for peripheral init to settle */
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Try to mount */
    while (!mounted) {
        if (sd_mount()) {
            mounted = true;
            debug_printf("[SD] Mounted OK\r\n");
        } else {
            debug_printf("[SD] Retrying in %d s...\r\n", SD_RETRY_DELAY_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(SD_RETRY_DELAY_MS));
        }
    }

    /* Generate unique filename from boot tick */
    snprintf(fname, sizeof(fname), "LOG_%05lu.CSV", xTaskGetTickCount() / 1000UL);
    debug_printf("[SD] Log file: %s\r\n", fname);

    if (!sd_write_header()) {
        debug_printf("[SD] Header write failed\r\n");
        mounted = false;
    }

    char row[192];
    SensorData_t data;
    uint32_t rows_written = 0;

    for (;;)
    {
        /* Block until a sensor reading is available (max 3s) */
        if (xQueueReceive(xSensorQueue, &data, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            snprintf(row, sizeof(row),
                "%lu,%.1f,%.1f,%.1f,%.2f,%.2f,%.1f,%.6f,%.6f,%.1f,%d,%d\n",
                (unsigned long)data.timestamp,
                data.temperature,
                data.humidity,
                data.mq135_ppm,
                data.turbidity_ntu,
                data.ph,
                data.tds_ppm,
                data.latitude,
                data.longitude,
                data.heading_deg,
                (int)data.gps_valid,
                (int)data.dht_valid);

            if (mounted) {
                if (sd_append(row)) {
                    rows_written++;
                    if (rows_written % 60 == 0) {
                        debug_printf("[SD] %lu rows written\r\n",
                                     (unsigned long)rows_written);
                    }
                } else {
                    /* Try remount once */
                    mounted = sd_mount();
                }
            }
        }
        /* If SD not mounted, keep draining queue to avoid blocking SensorTask */
    }
}
