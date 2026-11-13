#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
typedef void *QueueHandle_t;
typedef struct {int placeholder;} StaticQueue_t;
QueueHandle_t xQueueCreateStatic(unsigned capacity,unsigned item_size,uint8_t *memory,StaticQueue_t *control);
int xQueueSend(QueueHandle_t queue,const void *item,TickType_t timeout);
int xQueueReceive(QueueHandle_t queue,void *item,TickType_t timeout);
