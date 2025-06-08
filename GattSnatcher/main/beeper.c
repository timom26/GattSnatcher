#include <stdio.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BEEPING_INTERVAL 20000  // Beep every 20ms

void app_main(void)
{
    TickType_t currentTick, startTick = xTaskGetTickCount();
    printf("Timing started at: %lu\n", startTick);

    while (1) {
        usleep(BEEPING_INTERVAL);
        currentTick = xTaskGetTickCount();
        printf("Timestamp: %lu\n", (currentTick - startTick) * portTICK_PERIOD_MS);
    }
}