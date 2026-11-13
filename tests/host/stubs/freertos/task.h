#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);

typedef void *TaskHandle_t;
void vTaskDelay(TickType_t ticks);
int xTaskCreatePinnedToCore(void (*entry)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, TaskHandle_t *out, int core);

TaskHandle_t xTaskCreateStaticPinnedToCore(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,StackType_t *memory,StaticTask_t *control,int core);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(int clear,TickType_t timeout);
