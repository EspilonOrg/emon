#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "crash_guru";

void app_main(void)
{
    ESP_LOGI(TAG, "crash_guru: waiting 2s before crash...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "crash_guru: triggering Guru Meditation (NULL deref)...");

    /* NULL pointer dereference → LoadProhibited → Guru Meditation Error */
    volatile int *p = NULL;
    *p = 0xDEAD;
}
