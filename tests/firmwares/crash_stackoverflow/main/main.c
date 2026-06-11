#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "crash_sof";

/*
 * vApplicationStackOverflowHook - called by FreeRTOS at context switch
 * when it detects the stack pointer has gone below the task's stack base.
 * Prints the exact string our pattern matches, then halts.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    esp_rom_printf("***ERROR*** stack overflow in task %s\r\n", pcTaskName);
    while (1) { }
}

/*
 * Fill ~90% of the 2048-byte stack with a local buffer, then yield.
 * FreeRTOS PTRVAL check runs at the next context switch and detects
 * the stack pointer below the stack base → calls the hook above.
 */
static void overflow_task(void *arg)
{
    (void)arg;
    volatile char buf[1800];
    buf[0] = 1;
    (void)buf;
    /* Yield → context switch → PTRVAL check → overflow detected */
    vTaskDelay(1);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "crash_stackoverflow: waiting 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "crash_stackoverflow: spawning task with 2048B stack...");

    xTaskCreate(overflow_task, "overflow", 2048, NULL, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(10000));
}
