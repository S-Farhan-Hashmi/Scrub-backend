/*
 * task_gps.c
 * GPS NMEA parser on USART6 (PC7 RX) at 9600 baud.
 *
 * Parses:
 *   $GPRMC / $GNRMC  — position, fix status
 *   $GPGSV / $GNGSV  — satellites in view (count)
 *   $GPGSA / $GNGSA  — fix mode (1/2/3), sats used, PDOP/HDOP/VDOP
 *
 * All debug counters are written into g_gps_dbg (defined in task_debug.c).
 */

#include "tasks/task_debug.h"   /* debug_printf + GPS_Debug_State_t + g_gps_dbg */
#include "tasks/task_gps.h"
#include "app_types.h"
#include "drivers/gps_nmea.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Shared GPS position state (read by SensorTask / DebugTask) ── */
volatile float g_gps_lat   = 0.0f;
volatile float g_gps_lon   = 0.0f;
volatile bool  g_gps_valid = false;

/* ── DMA receive buffer ────────────────────────────────────────── */
#define GPS_DMA_BUF   512
static uint8_t  gps_dma_buf[GPS_DMA_BUF];
static uint16_t gps_last_pos = 0;

/* ── Line assembly ─────────────────────────────────────────────── */
#define GPS_LINE_MAX  100
static char    gps_line[GPS_LINE_MAX];
static uint8_t gps_line_idx = 0;

extern UART_HandleTypeDef huart6;

/* g_gps_dbg is declared extern via tasks/task_debug.h */

/* ------------------------------------------------------------------ */
/* NMEA field extractor (identical to gps_nmea.c helper)              */
/* ------------------------------------------------------------------ */
static void field(const char *s, int n, char *out, int maxlen)
{
    int f = 0, i = 0, j = 0;
    out[0] = '\0';
    while (s[i]) {
        if (s[i] == ',') {
            if (f == n) { out[j] = '\0'; return; }
            f++; j = 0;
        } else if (f == n) {
            if (j < maxlen - 1) out[j++] = s[i];
        }
        i++;
    }
    /* handle last field with no trailing comma */
    if (f == n) out[j] = '\0';
}

/* ------------------------------------------------------------------ */
/* $GPGSV / $GNGSV parser                                             */
/* Format: $GPGSV,<total msgs>,<msg#>,<sats in view>,...*CS           */
/* We only care about field 3 (index 3) from message #1              */
/* ------------------------------------------------------------------ */
static void parse_gsv(const char *sentence)
{
    char msg_num[4], siv[4];
    field(sentence, 2, msg_num, sizeof(msg_num));
    if (atoi(msg_num) != 1) return;           /* only first message has total */
    field(sentence, 3, siv, sizeof(siv));
    if (siv[0] == '\0') return;
    taskENTER_CRITICAL();
    g_gps_dbg.sats_in_view = (uint8_t)atoi(siv);
    taskEXIT_CRITICAL();
}

/* ------------------------------------------------------------------ */
/* $GPGSA / $GNGSA parser                                             */
/* Format:                                                            */
/*   $GPGSA,<Auto/M>,<fix mode>,<sv1>...<sv12>,<PDOP>,<HDOP>,<VDOP>*CS */
/* Field indices (0-based, counting $GPGSA as field 0):              */
/*   0=sentence  1=mode(A/M)  2=fix(1/2/3)                          */
/*   3-14=SV PRNs (may be empty)                                     */
/*   15=PDOP  16=HDOP  17=VDOP                                       */
/* ------------------------------------------------------------------ */
static void parse_gsa(const char *sentence)
{
    char fix[4];
    field(sentence, 2, fix, sizeof(fix));
    if (fix[0] == '\0') return;

    uint8_t fix_mode = (uint8_t)atoi(fix);

    /* Count non-empty SV fields (3-14) */
    uint8_t used = 0;
    char sv[6];
    for (int i = 3; i <= 14; i++) {
        field(sentence, i, sv, sizeof(sv));
        if (sv[0] != '\0') used++;
    }

    char pdop_s[8], hdop_s[8], vdop_s[8];
    field(sentence, 15, pdop_s, sizeof(pdop_s));
    field(sentence, 16, hdop_s, sizeof(hdop_s));

    /* VDOP may be followed by *CS, strip checksum */
    field(sentence, 17, vdop_s, sizeof(vdop_s));
    char *star = strchr(vdop_s, '*');
    if (star) *star = '\0';

    taskENTER_CRITICAL();
    g_gps_dbg.fix_mode   = fix_mode;
    g_gps_dbg.sats_used  = used;
    if (pdop_s[0]) g_gps_dbg.pdop = strtof(pdop_s, NULL);
    if (hdop_s[0]) g_gps_dbg.hdop = strtof(hdop_s, NULL);
    if (vdop_s[0]) g_gps_dbg.vdop = strtof(vdop_s, NULL);
    taskEXIT_CRITICAL();
}

/* ------------------------------------------------------------------ */
/* Extract complete NMEA sentence from DMA circular buffer            */
/* ------------------------------------------------------------------ */
static bool gps_get_line(void)
{
    uint16_t dma_pos =
        GPS_DMA_BUF - (uint16_t)__HAL_DMA_GET_COUNTER(huart6.hdmarx);

    while (gps_last_pos != dma_pos)
    {
        char c = (char)gps_dma_buf[gps_last_pos];
        gps_last_pos = (gps_last_pos + 1U) % GPS_DMA_BUF;

        if (c == '$')
        {
            gps_line_idx = 0;
            gps_line[gps_line_idx++] = '$';

            /* Count every sentence start */
            taskENTER_CRITICAL();
            g_gps_dbg.sentences_rx++;
            taskEXIT_CRITICAL();
        }
        else if (c == '\n')
        {
            if (gps_line_idx > 6)
            {
                gps_line[gps_line_idx] = '\0';
                gps_line_idx = 0;
                return true;
            }
            gps_line_idx = 0;
        }
        else if (c != '\r')
        {
            if (gps_line_idx > 0)
            {
                if (gps_line_idx < (GPS_LINE_MAX - 1))
                    gps_line[gps_line_idx++] = c;
                else
                    gps_line_idx = 0;   /* too long — discard & resync */
            }
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Task                                                               */
/* ------------------------------------------------------------------ */
void GPSTask(void *arg)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)arg;

    if (HAL_UART_Receive_DMA(huart, gps_dma_buf,
                              sizeof(gps_dma_buf)) != HAL_OK)
    {
        debug_printf("[GPS] DMA start FAILED\r\n");
        vTaskDelete(NULL);
    }

    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);

    debug_printf("[GPS] Task started — 9600 baud, USART6 (PC7 RX)\r\n");

    /* ── DIAGNOSTIC: raw blocking RX test (bypasses DMA entirely) ──────
     * We stop the DMA, attempt a plain blocking read for 3 seconds, then
     * restart DMA. This tells us if ANY bytes at all arrive on PC7.
     * Remove this block once GPS is confirmed working.
     * ─────────────────────────────────────────────────────────────────── */
    HAL_UART_DMAStop(huart);
    vTaskDelay(pdMS_TO_TICKS(100));

    debug_printf("[GPS-DIAG] Raw RX test — waiting 3 s for any byte on PC7...\r\n");
    uint8_t raw[8] = {0};
    HAL_StatusTypeDef rx_st = HAL_UART_Receive(huart, raw, 1, 3000);
    if (rx_st == HAL_OK) {
        debug_printf("[GPS-DIAG] GOT DATA: 0x%02X ('%c') — UART wiring OK!\r\n",
                     raw[0], (raw[0] >= 0x20 && raw[0] < 0x7F) ? raw[0] : '.');
        /* Grab a few more bytes to confirm NMEA stream */
        HAL_UART_Receive(huart, raw, sizeof(raw), 200);
        debug_printf("[GPS-DIAG] Next bytes: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                     raw[0],raw[1],raw[2],raw[3],raw[4],raw[5],raw[6],raw[7]);
    } else {
        debug_printf("[GPS-DIAG] TIMEOUT — no bytes received on PC7 in 3 s.\r\n");
        debug_printf("[GPS-DIAG] Check: GPS TX (yellow) -> PC7 | VCC=5V | GND common\r\n");
        debug_printf("[GPS-DIAG] Tip: confirm PC6/PC7 not swapped (PC6=USART6_TX)\r\n");
    }

    /* Restart DMA for normal operation */
    if (HAL_UART_Receive_DMA(huart, gps_dma_buf,
                              sizeof(gps_dma_buf)) != HAL_OK) {
        debug_printf("[GPS-DIAG] DMA restart FAILED\r\n");
        vTaskDelete(NULL);
    }
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    gps_last_pos = 0;
    debug_printf("[GPS] DMA restarted — entering NMEA parse loop\r\n");
    /* ── END DIAGNOSTIC ─────────────────────────────────────────────── */

    debug_printf("[GPS] Parsing: RMC (position), GSA (fix/DOP), GSV (sats)\r\n");

    uint32_t fix_count    = 0;
    uint32_t no_fix_count = 0;

    for (;;)
    {
        if (!gps_get_line())
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* ── Route sentence by type ── */
        if (strncmp(gps_line, "$GPGSV", 6) == 0 ||
            strncmp(gps_line, "$GNGSV", 6) == 0 ||
            strncmp(gps_line, "$GLGSV", 6) == 0)
        {
            parse_gsv(gps_line);
        }
        else if (strncmp(gps_line, "$GPGSA", 6) == 0 ||
                 strncmp(gps_line, "$GNGSA", 6) == 0)
        {
            parse_gsa(gps_line);
        }
        else if (strncmp(gps_line, "$GPRMC", 6) == 0 ||
                 strncmp(gps_line, "$GNRMC", 6) == 0)
        {
            GPS_Data_t gps;

            if (GPS_ParseRMC(gps_line, &gps))
            {
                taskENTER_CRITICAL();
                g_gps_lat              = gps.latitude;
                g_gps_lon              = gps.longitude;
                g_gps_valid            = gps.valid;
                g_gps_dbg.rmc_parsed++;
                g_gps_dbg.last_rmc_tick = xTaskGetTickCount();
                /* field 2 of RMC is status — extract directly */
                g_gps_dbg.last_status  = gps.valid ? 'A' : 'V';
                taskEXIT_CRITICAL();

                if (gps.valid)
                {
                    fix_count++;
                    if (fix_count == 1U || (fix_count % 10U) == 0U)
                    {
                        debug_printf(
                            "[GPS] Fix #%lu: %.6f, %.6f\r\n",
                            (unsigned long)fix_count,
                            gps.latitude,
                            gps.longitude);
                    }
                }
                else
                {
                    no_fix_count++;
                    if ((no_fix_count % 20U) == 0U)
                    {
                        debug_printf(
                            "[GPS] No fix (%lu void RMC)"
                            " — sats in view: %u\r\n",
                            (unsigned long)no_fix_count,
                            g_gps_dbg.sats_in_view);
                    }
                }
            }
        }
        /* All other sentences ($GPGLL, $GPVTG, $GPZDA, etc.) are silently
         * counted via sentences_rx above but otherwise ignored.           */

        vTaskDelay(pdMS_TO_TICKS(10));   /* yield — 9600 baud is slow */
    }
}
