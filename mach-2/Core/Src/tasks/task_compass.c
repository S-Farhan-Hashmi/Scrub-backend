/*
 * task_compass.c
 * HMC5883L / QMC5883L magnetometer on I2C1 (PB7=SDA, PB8=SCL).
 * Auto-detected by compass.c.
 *
 * Polls at 10 Hz, writes heading to g_heading_deg, and writes
 * raw XYZ + error counts into g_gps_dbg for the debug task.
 */

#include "tasks/task_compass.h"
#include "tasks/task_debug.h"       /* debug_printf + GPS_Debug_State_t + g_gps_dbg */
#include "app_types.h"
#include "drivers/compass.h"

/* Shared heading state */
volatile float g_heading_deg = 0.0f;

extern I2C_HandleTypeDef hi2c1;
/* g_gps_dbg is declared extern via tasks/task_debug.h */

/* ── Extended read that also returns raw XYZ ─────────────────────
 * We call the existing Compass_Read() which returns heading, then
 * re-read raw registers for the debug display. To keep it simple
 * we duplicate a 6-byte register read here rather than changing
 * the compass driver ABI.
 *
 * QMC5883L: regs 0x00-0x05 → XL XH YL YH ZL ZH
 * HMC5883L: regs 0x03-0x08 → XH XL ZH ZL YH YL
 * ──────────────────────────────────────────────────────────────── */
static void compass_read_raw(I2C_HandleTypeDef *hi2c,
                              int16_t *rx, int16_t *ry, int16_t *rz)
{
    uint8_t buf[6] = {0};
    CompassChip_t chip = Compass_GetChip();

    if (chip == COMPASS_CHIP_QMC5883L) {
        uint8_t reg = 0x00;
        if (HAL_I2C_Master_Transmit(hi2c, (0x0D << 1),
                                    &reg, 1, 20) == HAL_OK)
            HAL_I2C_Master_Receive(hi2c, (0x0D << 1),
                                   buf, 6, 20);
        *rx = (int16_t)((buf[1] << 8) | buf[0]);
        *ry = (int16_t)((buf[3] << 8) | buf[2]);
        *rz = (int16_t)((buf[5] << 8) | buf[4]);
    } else if (chip == COMPASS_CHIP_HMC5883L) {
        uint8_t reg = 0x03;
        if (HAL_I2C_Master_Transmit(hi2c, (0x1E << 1),
                                    &reg, 1, 20) == HAL_OK)
            HAL_I2C_Master_Receive(hi2c, (0x1E << 1),
                                   buf, 6, 20);
        *rx = (int16_t)((buf[0] << 8) | buf[1]);
        *rz = (int16_t)((buf[2] << 8) | buf[3]);
        *ry = (int16_t)((buf[4] << 8) | buf[5]);
    } else {
        *rx = *ry = *rz = 0;
    }
}
static void i2c_bus_recover(void)
{
    /* Temporarily reconfigure PB7(SDA) and PB8(SCL) as GPIO OD */
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7 | GPIO_PIN_8, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_7 | GPIO_PIN_8;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);

    /* 9 SCL pulses — enough to drain any in-progress byte */
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        osDelay(2);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        osDelay(2);
    }

    /* Generate STOP: SDA goes high while SCL is high */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    osDelay(2);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    osDelay(2);

    /* Re-init I2C peripheral (hi2c1 handle already has config from main) */
    HAL_I2C_DeInit(&hi2c1);
    osDelay(10);
    HAL_I2C_Init(&hi2c1);
    osDelay(10);
}
void CompassTask(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(2000U));
    if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(500U)) == pdTRUE) {
        i2c_bus_recover();
        xSemaphoreGive(xI2CMutex);
    }
    bool init_ok = false;

    /* ── DIAGNOSTIC: I2C bus scanner ───────────────────────────────────
     * Scans all 127 addresses and prints every device that ACKs.
     * Expected: 0x0D (QMC5883L) or 0x1E (HMC5883L).
     * If NOTHING responds: pull-ups missing or wiring open.
     * Remove once compass is confirmed working.
     * ─────────────────────────────────────────────────────────────────── */
    if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(1000U)) == pdTRUE)
    {
        debug_printf("[Compass-DIAG] Scanning I2C1 bus (SDA=PB7 SCL=PB8)...\r\n");
        uint8_t found = 0;
        for (uint8_t addr = 1; addr < 128; addr++)
        {
            if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1),
                                       2, 10) == HAL_OK)
            {
                debug_printf("[Compass-DIAG] ACK at 0x%02X%s\r\n", addr,
                             (addr == 0x0D) ? " <- QMC5883L" :
                             (addr == 0x1E) ? " <- HMC5883L" : "");
                found++;
            }
        }
        if (found == 0) {
            debug_printf("[Compass-DIAG] NO devices found.\r\n");
            debug_printf("[Compass-DIAG] Check: 4.7k pull-ups on PB7+PB8 to 3.3V\r\n");
            debug_printf("[Compass-DIAG] Check: JST2 white->PB8(SCL) orange->PB7(SDA)\r\n");
            debug_printf("[Compass-DIAG] Check: compass powered (VCC on JST1 red wire)\r\n");
        } else {
            debug_printf("[Compass-DIAG] Found %u device(s) on bus.\r\n", found);
        }
        xSemaphoreGive(xI2CMutex);
    }
    /* ── END DIAGNOSTIC ─────────────────────────────────────────────── */

    for (uint32_t attempt = 0; attempt < 5U; attempt++)
    {
        if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(500U)) == pdTRUE)
        {
            init_ok = Compass_Init(&hi2c1);
            xSemaphoreGive(xI2CMutex);
            if (init_ok) break;
        }
        vTaskDelay(pdMS_TO_TICKS(200U));
    }

    if (!init_ok) {
        debug_printf("[Compass] Init FAILED (SDA=PB7 SCL=PB8)\r\n");
    } else {
        CompassChip_t chip = Compass_GetChip();
        debug_printf("[Compass] Init OK — chip: %s\r\n",
                     (chip == COMPASS_CHIP_QMC5883L) ? "QMC5883L (0x0D)" :
                     (chip == COMPASS_CHIP_HMC5883L) ? "HMC5883L (0x1E)" :
                                                        "Unknown");
    }

    Compass_Data_t comp;

    for (;;)
    {
        if (!init_ok)
        {
            vTaskDelay(pdMS_TO_TICKS(5000U));

            if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200U)) == pdTRUE)
            {
                init_ok = Compass_Init(&hi2c1);
                xSemaphoreGive(xI2CMutex);
                if (init_ok)
                    debug_printf("[Compass] Re-init OK\r\n");
            }
            continue;
        }

        if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(100U)) == pdTRUE)
        {
            Compass_Read(&hi2c1, &comp);

            /* Read raw XYZ for debug display */
            int16_t rx, ry, rz;
            compass_read_raw(&hi2c1, &rx, &ry, &rz);

            xSemaphoreGive(xI2CMutex);

            if (comp.valid)
            {
                /* Check for overflow (both chips return -4096 on overflow) */
                bool overflow = (rx == -4096 || ry == -4096);

                taskENTER_CRITICAL();
                g_heading_deg        = comp.heading_deg;
                g_gps_dbg.cmp_raw_x  = rx;
                g_gps_dbg.cmp_raw_y  = ry;
                g_gps_dbg.cmp_raw_z  = rz;
                if (overflow) g_gps_dbg.cmp_overflows++;
                taskEXIT_CRITICAL();
            }
            else
            {
                taskENTER_CRITICAL();
                g_gps_dbg.cmp_read_errors++;
                taskEXIT_CRITICAL();

                debug_printf("[Compass] Read error #%lu — reinit\r\n",
                             (unsigned long)g_gps_dbg.cmp_read_errors);

                if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(500U)) == pdTRUE)
                {
                	i2c_bus_recover();
                    init_ok = Compass_Init(&hi2c1);
                    xSemaphoreGive(xI2CMutex);
                    if (init_ok)
                        debug_printf("[Compass] Re-init OK\r\n");
                }
            }
        }
        else
        {
            debug_printf("[Compass] I2C mutex timeout\r\n");
        }

        vTaskDelay(pdMS_TO_TICKS(100U));  /* 10 Hz */
    }
}
