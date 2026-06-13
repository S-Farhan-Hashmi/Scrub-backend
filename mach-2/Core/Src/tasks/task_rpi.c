/*
 * task_rpi.c
 * Autonomous navigation commands from Raspberry Pi via USART3.
 */

#include "tasks/task_debug.h"
#include "tasks/task_rpi.h"
#include "app_types.h"

#include <stdio.h>

#define RPI_LINE_MAX             64
#define RPI_DMA_BUF_SIZE         256
#define RPI_FLYSKY_OVERRIDE_MS   300

/* DMA circular buffer */
static uint8_t  rpi_dma_buf[RPI_DMA_BUF_SIZE];

/* Line assembly buffer */
static char     rpi_line[RPI_LINE_MAX];
static uint16_t rpi_last_pos = 0;

extern UART_HandleTypeDef huart3;

/* ------------------------------------------------------------------ */

static inline int16_t clamp_nav(int16_t v)
{
    if (v > 1000)  return 1000;
    if (v < -1000) return -1000;
    return v;
}

/* ------------------------------------------------------------------ */

static bool rpi_get_line(void)
{
    const uint16_t buf_len = sizeof(rpi_dma_buf);

    uint16_t dma_pos =
        buf_len - (uint16_t)__HAL_DMA_GET_COUNTER(huart3.hdmarx);

    static uint8_t idx = 0;

    while (rpi_last_pos != dma_pos)
    {
        char c = (char)rpi_dma_buf[rpi_last_pos];

        rpi_last_pos = (rpi_last_pos + 1) % buf_len;

        if ((c == '\n') || (c == '\r'))
        {
            if (idx > 0)
            {
                rpi_line[idx] = '\0';
                idx = 0;
                return true;
            }
        }
        else
        {
            if (idx < (RPI_LINE_MAX - 1))
            {
                rpi_line[idx++] = c;
            }
            else
            {
                /* Line too long -> discard and resync */
                idx = 0;
            }
        }
    }

    return false;
}

/* ------------------------------------------------------------------ */

static bool parse_rpi_cmd(const char *line, NavCommand_t *cmd)
{
    int16_t t = 0;
    int16_t s = 0;

    if (sscanf(line, "T:%hd,S:%hd", &t, &s) != 2)
    {
        return false;
    }

    t = clamp_nav(t);
    s = clamp_nav(s);

    cmd->source    = NAV_SOURCE_RPI;
    cmd->throttle  = t;
    cmd->steering  = s;
    cmd->timestamp = xTaskGetTickCount();

    return true;
}

/* ------------------------------------------------------------------ */

void RpiUARTTask(void *arg)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)arg;

    if (HAL_UART_Receive_DMA(
            huart,
            rpi_dma_buf,
            sizeof(rpi_dma_buf)) != HAL_OK)
    {
        debug_printf("[RPi] DMA start FAILED\r\n");
        vTaskDelete(NULL);
    }

    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);

    debug_printf("[RPi] Task started\r\n");

    for (;;)
    {
        while (rpi_get_line())
        {
            TickType_t since_flysky =
                xTaskGetTickCount() - g_last_flysky_tick;

            if (since_flysky >
                pdMS_TO_TICKS(RPI_FLYSKY_OVERRIDE_MS))
            {
                NavCommand_t cmd;

                if (parse_rpi_cmd(rpi_line, &cmd))
                {

                    /* Recommended: queue length = 1 */
                    xQueueOverwrite(xNavQueue, &cmd);



                    debug_printf(
                        "[RPi] CMD T:%d S:%d\r\n",
                        cmd.throttle,
                        cmd.steering);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
