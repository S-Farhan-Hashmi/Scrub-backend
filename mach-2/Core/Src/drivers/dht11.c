/*
 * dht11.c
 * Bit-bang DHT11 driver using DWT cycle counter for µs timing.
 * PC0 → DHT11 data line (external 4.7kΩ pull-up to 3.3V required).
 *
 * Timing reference (DHT11 datasheet):
 *   Host start:  pull low ≥18ms, then release
 *   Sensor resp: pull low 80µs, then high 80µs
 *   Bit '0':     low 50µs, high 26–28µs
 *   Bit '1':     low 50µs, high 70µs
 *   Decision threshold: if high pulse > 40µs → '1', else '0'
 */

#include "drivers/dht11.h"
#include "cmsis_os.h"

/* ── DWT µs delay ─────────────────────────────────────────────────────── */
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks);
}

/* ── Pin helpers ─────────────────────────────────────────────────────── */
static void pin_output(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = DHT11_GPIO_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &g);
}

static void pin_input(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = DHT11_GPIO_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &g);
}

static inline void pin_low(void)  { HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_RESET); }
static inline void pin_high(void) { HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_SET);   }
static inline uint8_t pin_read(void) { return HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN); }

/* ── Wait for pin state with timeout (µs) ───────────────────────────── */
static bool wait_pin(uint8_t state, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t limit = timeout_us * (SystemCoreClock / 1000000U);
    while (pin_read() != state) {
        if ((DWT->CYCCNT - start) >= limit) return false;
    }
    return true;
}

/* ── Public API ──────────────────────────────────────────────────────── */
void DHT11_Init(void)
{
    /* Ensure DWT is running — also done in main USER CODE BEGIN 2 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    pin_input();   /* idle: line pulled high externally */
    osDelay(2000); /* wait 2s on power-up for DHT11 to stabilise */
}

DHT11_Status_t DHT11_Read(DHT11_Data_t *out)
{
    uint8_t data[5] = {0};

    /* ── 1. Host start signal ── */
    pin_output();
    pin_low();
    osDelay(20);           /* ≥18ms low */
    pin_high();
    delay_us(30);          /* 20–40µs high before releasing */
    pin_input();

    /* ── 2. Sensor response ── */
    if (!wait_pin(GPIO_PIN_RESET, 100)) return DHT11_ERR_TIMEOUT;  /* low 80µs */
    if (!wait_pin(GPIO_PIN_SET,   100)) return DHT11_ERR_TIMEOUT;  /* high 80µs */
    if (!wait_pin(GPIO_PIN_RESET, 100)) return DHT11_ERR_TIMEOUT;  /* falling edge = data start */

    /* ── 3. Read 40 bits ── */
    for (int i = 0; i < 40; i++) {
        /* Each bit starts with a ~50µs low pulse */
        if (!wait_pin(GPIO_PIN_SET, 70)) return DHT11_ERR_TIMEOUT;  /* wait for rising edge */

        /* Measure high pulse width to determine bit value */
        delay_us(40);   /* wait 40µs: if still high → bit=1, else bit=0 */

        data[i / 8] <<= 1;
        if (pin_read() == GPIO_PIN_SET) {
            data[i / 8] |= 0x01;
        }

        /* Wait for falling edge before next bit */
        if (!wait_pin(GPIO_PIN_RESET, 100)) return DHT11_ERR_TIMEOUT;
    }

    /* ── 4. Checksum ── */
    uint8_t sum = data[0] + data[1] + data[2] + data[3];
    if (sum != data[4]) return DHT11_ERR_CHECKSUM;

    /* ── 5. Extract values ── */
    /* DHT11: integer + decimal bytes for humidity and temperature */
    out->humidity    = (float)data[0] + (float)data[1] * 0.1f;
    out->temperature = (float)data[2] + (float)data[3] * 0.1f;

    return DHT11_OK;
}
