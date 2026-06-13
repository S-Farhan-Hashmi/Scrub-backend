/*
 * task_motor.c
 * DC motor control — 2× motors via L298N or similar driver.
 *
 * Hardware:
 *   PA8  → TIM1_CH1 → Motor A PWM (left)
 *   PA9  → TIM1_CH2 → Motor B PWM (right)
 *   PB0  → DIR_A   (HIGH = forward)
 *   PB1  → DIR_B   (HIGH = forward)
 *
 * TIM1: PSC=179, ARR=999 → 1 kHz PWM, CCR range 0–999.
 *
 * Mixing:
 *   left  = throttle + steering
 *   right = throttle - steering
 * Values clamped to ±999 then mapped to direction + magnitude.
 *
 * Safety: if no NavCommand received for MOTOR_TIMEOUT_MS, stop motors.
 * NOTE: Raspberry Pi is assumed NOT connected — RPi commands are accepted
 *       in the queue but do not change the safety behaviour.
 */
#include "tasks/task_debug.h"
#include "tasks/task_motor.h"
#include "app_types.h"
#include <math.h>
#define MOTOR_TIMEOUT_MS   300   /* stop if no command within this period */
#define PWM_MAX            999   /* matches TIM1 ARR */

#define DIR_A_PORT   GPIOB
#define DIR_A_PIN    GPIO_PIN_0
#define DIR_B_PORT   GPIOB
#define DIR_B_PIN    GPIO_PIN_1

extern TIM_HandleTypeDef htim1;

/* ── Set a single motor: speed -999..+999 ─────────────────────────── */
static void motor_set(uint32_t channel,
                      GPIO_TypeDef *dir_port, uint16_t dir_pin,
                      int32_t speed)
{
    if (speed > PWM_MAX)  speed =  PWM_MAX;
    if (speed < -PWM_MAX) speed = -PWM_MAX;

    if (speed >= 0) {
        HAL_GPIO_WritePin(dir_port, dir_pin, GPIO_PIN_SET);
        __HAL_TIM_SET_COMPARE(&htim1, channel, (uint32_t)speed);
    } else {
        HAL_GPIO_WritePin(dir_port, dir_pin, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim1, channel, (uint32_t)(-speed));
    }
}

static void motors_stop(void)
{
    HAL_GPIO_WritePin(DIR_A_PORT, DIR_A_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DIR_B_PORT, DIR_B_PIN, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
}

/* ── Task ────────────────────────────────────────────────────────────── */
void MotorTask(void *arg)
{
    (void)arg;

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    motors_stop();

    debug_printf("[Motor] Task started\r\n");

    NavCommand_t cmd;
    TickType_t   last_cmd_tick = 0;
    bool         armed         = false;   /* require at least one valid cmd */

    for (;;)
    {
        if (xQueueReceive(xNavQueue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            armed         = true;
            last_cmd_tick = xTaskGetTickCount();

            /* Scale ±1000 input → ±999 PWM — simple differential mix */
            int32_t thr = (int32_t)cmd.throttle * PWM_MAX / 1000;
            int32_t str = (int32_t)cmd.steering  * PWM_MAX / 1000;

            int32_t left;
            int32_t right;
            left  = thr + str;
            right = thr - str;

            int32_t abs_left  = (left  >= 0) ? left  : -left;
            int32_t abs_right = (right >= 0) ? right : -right;

            int32_t maxmag = (abs_left > abs_right)
                           ? abs_left
                           : abs_right;

            if (maxmag > PWM_MAX)
            {
                left  = (left  * PWM_MAX) / maxmag;
                right = (right * PWM_MAX) / maxmag;
            }

            motor_set(TIM_CHANNEL_1, DIR_A_PORT, DIR_A_PIN, left);
            motor_set(TIM_CHANNEL_2, DIR_B_PORT, DIR_B_PIN, right);
        }
        else
        {
            /* Queue timeout: check command freshness */
            if (armed) {
                TickType_t age = xTaskGetTickCount() - last_cmd_tick;
                if (age > pdMS_TO_TICKS(MOTOR_TIMEOUT_MS)) {
                    motors_stop();
                    armed = false;
                    debug_printf("[Motor] Timeout — stopped\r\n");
                }
            }
        }
    }
}
