/*
 * compass.c
 * Auto-detects HMC5883L (0x1E) or QMC5883L (0x0D).
 * NEO-M8N APM modules use QMC5883L.
 *
 * HMC5883L register map:
 *   0x00 Config A, 0x01 Config B, 0x02 Mode
 *   0x03-0x08 Data: XH XL ZH ZL YH YL  (MSB first)
 *   0x0A-0x0C ID: 'H' '4' '3'
 *
 * QMC5883L register map:
 *   0x00-0x05 Data: XL XH YL YH ZL ZH  (LSB first, XYZ order)
 *   0x09 Control 1 (mode/ODR/range/OSR)
 *   0x0A Control 2
 *   0x0B SET/RESET period
 *   0x0D Chip ID (reads 0xFF)
 */

#include "drivers/compass.h"
#include "cmsis_os.h"
#include <math.h>

#define I2C_TIMEOUT    100U   /* ms */

static CompassChip_t s_chip = COMPASS_CHIP_UNKNOWN;

/* ── Low-level helpers ───────────────────────────────────────── */
static bool i2c_write_reg(I2C_HandleTypeDef *hi2c,
                           uint16_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return HAL_I2C_Master_Transmit(hi2c, addr, buf, 2, I2C_TIMEOUT) == HAL_OK;
}

static bool i2c_read_reg(I2C_HandleTypeDef *hi2c,
                          uint16_t addr, uint8_t reg,
                          uint8_t *buf, uint8_t len)
{
    if (HAL_I2C_Master_Transmit(hi2c, addr, &reg, 1, I2C_TIMEOUT) != HAL_OK)
        return false;
    return HAL_I2C_Master_Receive(hi2c, addr, buf, len, I2C_TIMEOUT) == HAL_OK;
}

/* ── HMC5883L init ───────────────────────────────────────────── */
static bool hmc_init(I2C_HandleTypeDef *hi2c)
{
    /* Verify ID registers read "H43" */
    uint8_t id[3] = {0};
    if (!i2c_read_reg(hi2c, HMC5883L_ADDR, 0x0A, id, 3)) return false;
    if (id[0] != 'H' || id[1] != '4' || id[2] != '3')    return false;

    /* Config A: 8 samples avg, 15Hz, normal measurement */
    if (!i2c_write_reg(hi2c, HMC5883L_ADDR, 0x00, 0x70)) return false;
    /* Config B: gain 1090 LSB/Gauss (±1.3 Ga) */
    if (!i2c_write_reg(hi2c, HMC5883L_ADDR, 0x01, 0x20)) return false;
    /* Mode: continuous */
    if (!i2c_write_reg(hi2c, HMC5883L_ADDR, 0x02, 0x00)) return false;

    osDelay(10);
    return true;
}

/* ── QMC5883L init ───────────────────────────────────────────── */
static bool qmc_init(I2C_HandleTypeDef *hi2c)
{
    /* Read chip ID — QMC5883L returns 0xFF at register 0x0D */
    uint8_t id = 0;
    if (!i2c_read_reg(hi2c, QMC5883L_ADDR, 0x0D, &id, 1)) return false;
    if (id != 0xFF) return false;

    /* SET/RESET period — must be written first (datasheet requirement) */
    if (!i2c_write_reg(hi2c, QMC5883L_ADDR, 0x0B, 0x01)) return false;

    /*
     * Control Register 1:
     *   Bits[1:0] MODE = 01  continuous measurement
     *   Bits[3:2] ODR  = 10  100 Hz output rate
     *   Bits[5:4] RNG  = 00  ±2 Gauss range
     *   Bits[7:6] OSR  = 00  512 samples (best noise suppression)
     *   Value = 0b00_00_10_01 = 0x09
     */
    if (!i2c_write_reg(hi2c, QMC5883L_ADDR, 0x09, 0x09)) return false;

    /* Control Register 2: interrupts disabled, no soft reset */
    if (!i2c_write_reg(hi2c, QMC5883L_ADDR, 0x0A, 0x00)) return false;

    osDelay(10);
    return true;
}

/* ── HMC5883L read ───────────────────────────────────────────── */
static bool hmc_read(I2C_HandleTypeDef *hi2c, Compass_Data_t *out)
{
    uint8_t buf[6];
    /* Data register 0x03: XH XL ZH ZL YH YL */
    if (!i2c_read_reg(hi2c, HMC5883L_ADDR, 0x03, buf, 6))
    {
        out->valid = false;
        return false;
    }

    int16_t x = (int16_t)((buf[0] << 8) | buf[1]);
    /* z = buf[2..3] — not used for 2D heading */
    int16_t y = (int16_t)((buf[4] << 8) | buf[5]);

    /* Overflow check */
    if (x == -4096 || y == -4096)
    {
        out->valid = false;
        return false;
    }

    float heading = atan2f((float)y, (float)x) * (180.0f / (float)M_PI);
    if (heading < 0.0f) heading += 360.0f;

    out->heading_deg = heading;
    out->valid       = true;
    out->chip        = COMPASS_CHIP_HMC5883L;
    return true;
}

/* ── QMC5883L read ───────────────────────────────────────────── */
static bool qmc_read(I2C_HandleTypeDef *hi2c, Compass_Data_t *out)
{
    uint8_t buf[6];
    /* Data register 0x00: XL XH YL YH ZL ZH */
    if (!i2c_read_reg(hi2c, QMC5883L_ADDR, 0x00, buf, 6))
    {
        out->valid = false;
        return false;
    }

    /* QMC5883L is LSB first — opposite byte order from HMC5883L */
    int16_t x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t y = (int16_t)((buf[3] << 8) | buf[2]);
    /* z = buf[4..5] — not used */

    /* Overflow: QMC returns -4096 on overflow same as HMC */
    if (x == -4096 || y == -4096)
    {
        out->valid = false;
        return false;
    }

    float heading = atan2f((float)y, (float)x) * (180.0f / (float)M_PI);
    if (heading < 0.0f) heading += 360.0f;

    out->heading_deg = heading;
    out->valid       = true;
    out->chip        = COMPASS_CHIP_QMC5883L;
    return true;
}

/* ── Public API ──────────────────────────────────────────────── */
bool Compass_Init(I2C_HandleTypeDef *hi2c)
{
    /* Try QMC5883L first — more common on NEO-M8N APM modules */
    if (qmc_init(hi2c))
    {
        s_chip = COMPASS_CHIP_QMC5883L;
        return true;
    }

    /* Fallback: try HMC5883L */
    if (hmc_init(hi2c))
    {
        s_chip = COMPASS_CHIP_HMC5883L;
        return true;
    }

    s_chip = COMPASS_CHIP_UNKNOWN;
    return false;
}

bool Compass_Read(I2C_HandleTypeDef *hi2c, Compass_Data_t *out)
{
    switch (s_chip)
    {
        case COMPASS_CHIP_QMC5883L: return qmc_read(hi2c, out);
        case COMPASS_CHIP_HMC5883L: return hmc_read(hi2c, out);
        default:
            out->valid = false;
            return false;
    }
}

CompassChip_t Compass_GetChip(void)
{
    return s_chip;
}
