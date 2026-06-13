#ifndef APP_TYPES_H
#define APP_TYPES_H

#include "cmsis_os.h"
#include "FreeRTOS.h"  /* Required for FreeRTOS types */
#include "queue.h"     /* Required for QueueHandle_t */
#include "semphr.h"    /* Required for SemaphoreHandle_t */
#include <stdint.h>
#include <stdbool.h>
//extern volatile float g_heading_deg;
//extern volatile float g_gps_lat;
//extern volatile float g_gps_lon;
//extern volatile bool  g_gps_valid;
//
//extern volatile TickType_t g_last_flysky_tick;
/* ── Navigation ── */
typedef enum {
    NAV_SOURCE_FLYSKY = 0,
    NAV_SOURCE_RPI,
} NavSource_t;

typedef struct {
    NavSource_t source;
    int16_t     throttle;   // -1000 to +1000
    int16_t     steering;   // -1000 to +1000
    uint32_t    timestamp;  // xTaskGetTickCount()
} NavCommand_t;

/* ── Sensors ── */
typedef struct {
    float    temperature;   // DHT11 °C
    float    humidity;      // DHT11 %
    bool     dht_valid;     // Added: Missing dht flag
    float    mq135_ppm;     // air quality
    float    turbidity_ntu;
    float    ph;
    float    tds_ppm;
    float    latitude;
    float    longitude;
    bool     gps_valid;     // Added: Missing gps flag
    float    heading_deg;   // compass
    uint32_t timestamp;
} SensorData_t;

/* ── Motor ── */
typedef struct {
    uint16_t pwm_a;         // 0–1000 TIM ARR units
    uint16_t pwm_b;
    bool     dir_a;
    bool     dir_b;
} MotorCmd_t;

/* ── Global Handles (extern in main.c) ── */
extern QueueHandle_t  xNavQueue;
extern QueueHandle_t  xSensorQueue;
extern SemaphoreHandle_t xI2CMutex;
extern SemaphoreHandle_t xSDMutex;
extern SemaphoreHandle_t xUART2Mutex;

/* ── Shared Volatile State (extern in main.c) ── */
extern volatile float      g_gps_lat;
extern volatile float      g_gps_lon;
extern volatile bool       g_gps_valid;
extern volatile float      g_heading_deg;
extern volatile TickType_t g_last_flysky_tick;
extern volatile int16_t    g_flysky_throttle;
extern volatile int16_t    g_flysky_steering;
#endif
