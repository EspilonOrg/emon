#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "crash_abort";

void app_main(void)
{
    ESP_LOGI(TAG, "crash_abort: waiting 2s before crash...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "crash_abort: calling abort()...");
    abort();
}
