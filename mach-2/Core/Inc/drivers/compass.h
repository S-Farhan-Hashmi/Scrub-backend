#ifndef COMPASS_H
#define COMPASS_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ── I2C addresses (7-bit, shifted left for HAL) ─────────────── */
#define HMC5883L_ADDR   (0x1E << 1)   /* original Honeywell chip  */
#define QMC5883L_ADDR   (0x0D << 1)   /* clone — used on NEO-M8N  */

typedef enum {
    COMPASS_CHIP_UNKNOWN  = 0,
    COMPASS_CHIP_HMC5883L,
    COMPASS_CHIP_QMC5883L,
} CompassChip_t;

typedef struct {
    float        heading_deg;
    bool         valid;
    CompassChip_t chip;   /* detected chip type, for debug */
} Compass_Data_t;

bool Compass_Init(I2C_HandleTypeDef *hi2c);
bool Compass_Read(I2C_HandleTypeDef *hi2c, Compass_Data_t *out);

/* Returns which chip was detected after Compass_Init */
CompassChip_t Compass_GetChip(void);

#endif /* COMPASS_H */
