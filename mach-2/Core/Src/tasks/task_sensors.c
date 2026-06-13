/*
 * task_sensors.c
 * Reads all analog sensors via ADC1 DMA and DHT11 via bit-bang.
 */

#include "tasks/task_debug.h"
#include "tasks/task_sensors.h"
#include "app_types.h"
#include "drivers/dht11.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ADC_CHANNELS       4
#define ADC_VREF           3.3f
#define ADC_FULLSCALE      4095.0f

/* Indexes into DMA buffer */
#define IDX_MQ135          0
#define IDX_TURBIDITY      1
#define IDX_PH             2
#define IDX_TDS            3

/* DMA destination */
static volatile uint16_t adc_buf[ADC_CHANNELS];
static volatile bool     adc_done = false;

extern ADC_HandleTypeDef hadc1;

/* ------------------------------------------------------------------ */
/* ADC DMA callback                                                   */
/* ------------------------------------------------------------------ */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_done = true;
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline float raw_to_volt(uint16_t raw)
{
    return ((float)raw * ADC_VREF) / ADC_FULLSCALE;
}

static float calc_mq135(uint16_t raw)
{
    float volt = raw_to_volt(raw);

    if (volt < 0.1f)
    {
        return 0.0f;
    }

    float ppm =
        116.6020682f *
        powf((volt / 0.5f), -2.769034857f);

    return (ppm < 0.0f) ? 0.0f : ppm;
}

static float calc_turbidity(uint16_t raw)
{
    float volt = raw_to_volt(raw);
    float v5   = volt * (5.0f / 3.3f);
    float ntu  = (-1120.4f * v5 * v5) + (5742.3f * v5) - 4352.9f;
    return (ntu < 0.0f) ? 0.0f : ntu;
}

static float calc_ph(uint16_t raw)
{
    float volt = raw_to_volt(raw);
    float ph   = 7.0f + ((2.5f - volt) / 0.18f);

    if (ph < 0.0f)  ph = 0.0f;
    if (ph > 14.0f) ph = 14.0f;

    return ph;
}

static float calc_tds(uint16_t raw, float temperature_c)
{
    float volt = raw_to_volt(raw);
    float comp = 1.0f + (0.02f * (temperature_c - 25.0f));
    float vc   = volt / comp;
    float tds  = ((133.42f * vc * vc * vc)
                - (255.86f * vc * vc)
                + (857.39f * vc)) * 0.5f;
    return (tds < 0.0f) ? 0.0f : tds;
}

/* ------------------------------------------------------------------ */
/* ADC read                                                           */
/* ------------------------------------------------------------------ */

static bool adc_read_blocking(uint32_t timeout_ms)
{
    adc_done = false;

    if (HAL_ADC_Start_DMA(
            &hadc1,
            (uint32_t *)adc_buf,
            ADC_CHANNELS) != HAL_OK)
    {
        debug_printf("[Sensor] ADC DMA start failed\r\n");
        return false;
    }

    TickType_t start = xTaskGetTickCount();

    while (!adc_done)
    {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms))
        {
            HAL_ADC_Stop_DMA(&hadc1);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    HAL_ADC_Stop_DMA(&hadc1);
    return true;
}

/* ------------------------------------------------------------------ */
/* Task                                                               */
/* ------------------------------------------------------------------ */

void SensorTask(void *arg)
{
    (void)arg;

    DHT11_Init();

    SensorData_t data = {0};

    /*
     * tds_temp: temperature used for TDS compensation.
     * Kept separate from data.temperature so a DHT11 failure (which
     * sets data.temperature = NAN for the log) does NOT propagate NAN
     * into the TDS calculation on the next ADC cycle.
     * Updated only on a successful DHT11 read.
     */
    float tds_temp = 25.0f;

    /* Also initialise data.temperature so the first log row is valid */
    data.temperature = 25.0f;

    uint32_t dht_counter = 0;

    debug_printf("[Sensor] Task started\r\n");

    for (;;)
    {
        /* ---------------------------------------------------------- */
        /* ADC sensors — uses tds_temp, never data.temperature        */
        /* ---------------------------------------------------------- */

        if (adc_read_blocking(200))
        {
            data.mq135_ppm     = calc_mq135    (adc_buf[IDX_MQ135]);
            data.turbidity_ntu = calc_turbidity(adc_buf[IDX_TURBIDITY]);
            data.ph            = calc_ph        (adc_buf[IDX_PH]);

            /* TDS uses tds_temp — last confirmed good temperature */
            data.tds_ppm       = calc_tds(adc_buf[IDX_TDS], tds_temp);
        }
        else
        {
            debug_printf("[Sensor] ADC timeout\r\n");
        }

        /* ---------------------------------------------------------- */
        /* DHT11 every 2 seconds                                      */
        /* ---------------------------------------------------------- */

        dht_counter++;

        if (dht_counter >= 2U)
        {
            dht_counter = 0;

            DHT11_Data_t   dht;
            DHT11_Status_t st = DHT11_Read(&dht);

            if (st == DHT11_OK)
            {
                data.temperature = dht.temperature;
                data.humidity    = dht.humidity;
                data.dht_valid   = true;

                /* Update TDS compensation temperature only on success */
                tds_temp = dht.temperature;
            }
            else
            {
                /*
                 * Mark reading invalid and write NAN to the log so the
                 * CSV clearly shows a failed sample rather than stale data.
                 * tds_temp is intentionally NOT updated — it keeps its
                 * last good value so TDS stays valid.
                 */
                data.temperature = NAN;
                data.humidity    = NAN;
                data.dht_valid   = false;

                if (st == DHT11_ERR_CHECKSUM)
                {
                    debug_printf("[Sensor] DHT11 checksum error\r\n");
                }
                else
                {
                    debug_printf("[Sensor] DHT11 timeout\r\n");
                }
            }
        }

        /* ---------------------------------------------------------- */
        /* Atomic snapshot of GPS + Compass                           */
        /* ---------------------------------------------------------- */

        taskENTER_CRITICAL();

        data.latitude    = g_gps_lat;
        data.longitude   = g_gps_lon;
        data.gps_valid   = g_gps_valid;
        data.heading_deg = g_heading_deg;

        taskEXIT_CRITICAL();

        data.timestamp = xTaskGetTickCount();

        /* ---------------------------------------------------------- */
        /* Publish latest sensor sample                               */
        /* ---------------------------------------------------------- */

        xQueueOverwrite(xSensorQueue, &data);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
