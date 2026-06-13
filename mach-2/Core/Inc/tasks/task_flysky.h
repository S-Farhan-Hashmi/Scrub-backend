#ifndef TASK_FLYSKY_H
#define TASK_FLYSKY_H
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    TIM_HandleTypeDef *htim;
    uint32_t           channel;
} FlySkyTaskArg_t;

void FlySkyTask(void *arg);
#endif
