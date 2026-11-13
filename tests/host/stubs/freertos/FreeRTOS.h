#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define portTICK_PERIOD_MS 1U

#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define portENTER_CRITICAL_ISR(lock) ((void)(lock))
#define portEXIT_CRITICAL_ISR(lock) ((void)(lock))

#include <stdbool.h>
#define pdTRUE 1
#define pdFALSE 0
typedef uint32_t StackType_t;
typedef struct {int placeholder;} StaticTask_t;
bool xPortInIsrContext(void);
