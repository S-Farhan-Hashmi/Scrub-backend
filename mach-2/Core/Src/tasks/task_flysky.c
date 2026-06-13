/*
 * task_flysky.c
 * FlySky i6 + FSIA10B — PPM decoder via TIM2 input capture on PA0.
 *
 * PPM frame (8 channels, rising-to-rising):
 *   CH1=Roll CH2=Pitch CH3=Throttle CH4=Yaw CH5-8=aux
 *   SYNC gap > 3000µs marks end of frame
 *
 * Navigation mapping:
 *   Throttle → CH3 (index 2) — left stick vertical
 *   Steering → CH4 (index 3) — left stick horizontal
 */

#include "tasks/task_flysky.h"
#include "app_types.h"
#include "tasks/task_debug.h"
#include <string.h>

/* ── Channel config ──────────────────────────────────────────── */
#define PPM_CHANNELS_MAX     8
#define PPM_SYNC_MIN_US      3000U
#define PPM_PULSE_MIN_US      800U
#define PPM_PULSE_MAX_US     2200U

#define PPM_CH_THROTTLE      0     /* CH3 — left stick vertical   */
#define PPM_CH_STEERING      1     /* CH4 — left stick horizontal */

/* ── Shared state written by ISR, read by task ───────────────── */
static volatile uint16_t ppm_raw[PPM_CHANNELS_MAX];
static volatile bool     ppm_frame_ready  = false;
static volatile uint8_t  ppm_channel_idx  = 0;
static volatile uint32_t ppm_prev_capture = 0;

/* ── Public globals ──────────────────────────────────────────── */
volatile TickType_t g_last_flysky_tick = 0;
volatile int16_t    g_flysky_throttle  = 0;
volatile int16_t    g_flysky_steering  = 0;

extern TIM_HandleTypeDef htim2;

/* ── TIM2 capture callback — called from TIM2_IRQHandler ─────── */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2 ||
        htim->Channel  != HAL_TIM_ACTIVE_CHANNEL_1)
        return;

    uint32_t now   = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    uint32_t delta = (now >= ppm_prev_capture)
                     ? (now - ppm_prev_capture)
                     : (0xFFFFFFFFU - ppm_prev_capture + now + 1U);
    ppm_prev_capture = now;

    if (delta >= PPM_SYNC_MIN_US)
    {
        /* End of frame — mark ready if we got enough channels */
        if (ppm_channel_idx >= 4U)
            ppm_frame_ready = true;
        ppm_channel_idx = 0;
    }
    else if (delta >= PPM_PULSE_MIN_US && delta <= PPM_PULSE_MAX_US)
    {
        if (ppm_channel_idx < PPM_CHANNELS_MAX)
        {
            ppm_raw[ppm_channel_idx] = (uint16_t)delta;
            ppm_channel_idx++;
        }
    }
}

/* ── Map 1000–2000µs → -1000..+1000 ─────────────────────────── */
static int16_t ppm_to_centered(uint16_t us)
{
    if (us < 1000U) us = 1000U;
    if (us > 2000U) us = 2000U;
    return (int16_t)(((int32_t)us - 1500) * 2);
}

/* ── Task ────────────────────────────────────────────────────── */
void FlySkyTask(void *arg)
{
    (void)arg;

    /* Start TIM2 input capture interrupt */
    if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1) != HAL_OK)
    {
        debug_printf("[FlySky] FATAL: TIM2 IC start failed\r\n");
        vTaskDelete(NULL);
    }

    debug_printf("[FlySky] PPM decoder started on PA0 (TIM2_CH1)\r\n");

    uint32_t frame_count   = 0;
    uint32_t no_signal_log = 0;

    for (;;)
    {
        if (ppm_frame_ready)
        {
            ppm_frame_ready = false;
            frame_count++;

            /* Safely copy volatile channel values */
            uint16_t raw_thr, raw_str;
            taskENTER_CRITICAL();
            raw_thr = ppm_raw[PPM_CH_THROTTLE];
            raw_str = ppm_raw[PPM_CH_STEERING];
            taskEXIT_CRITICAL();

            int16_t throttle = ppm_to_centered(raw_thr);
            int16_t steering = ppm_to_centered(raw_str);

            g_flysky_throttle  = throttle;
            g_flysky_steering  = steering;
            g_last_flysky_tick = xTaskGetTickCount();

            NavCommand_t cmd = {
                .source    = NAV_SOURCE_FLYSKY,
                .throttle  = throttle,
                .steering  = steering,
                .timestamp = xTaskGetTickCount(),
            };
            xQueueOverwrite(xNavQueue, &cmd);

            /* Log first 3 frames then every 200th */
            if (frame_count <= 3U || frame_count % 200U == 0U)
            {
                debug_printf(
                    "[FlySky] Frame #%lu T:%d S:%d "
                    "(raw thr:%u str:%u)\r\n",
                    (unsigned long)frame_count,
                    throttle, steering,
                    raw_thr, raw_str);
            }
        }
        else
        {
            /* Check for signal loss */
            uint32_t age = xTaskGetTickCount() - g_last_flysky_tick;
            if (age > pdMS_TO_TICKS(500U) && frame_count > 0U)
            {
                no_signal_log++;
                if (no_signal_log % 20U == 1U)
                    debug_printf("[FlySky] Signal lost\r\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
