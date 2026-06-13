#ifndef DHT11_H
#define DHT11_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ── Pin ── change if your CubeMX label differs ─────────────────────── */
#define DHT11_GPIO_PORT   DHT_PIN_GPIO_Port   /* PC0 */
#define DHT11_GPIO_PIN    DHT_PIN_Pin

typedef enum {
    DHT11_OK = 0,
    DHT11_ERR_TIMEOUT,
    DHT11_ERR_CHECKSUM,
} DHT11_Status_t;

typedef struct {
    float temperature;   /* °C  */
    float humidity;      /* %   */
} DHT11_Data_t;

void           DHT11_Init(void);
DHT11_Status_t DHT11_Read(DHT11_Data_t *out);

#endif /* DHT11_H */
