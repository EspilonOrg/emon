#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "test_espidf";

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Trigger ESP_WARN pattern: ^W \([0-9]+\) */
    ESP_LOGW(TAG, "this is a warning - ESP_WARN should fire");
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Trigger ESP_ERROR pattern: ^E \([0-9]+\) */
    ESP_LOGE(TAG, "this is an error - ESP_ERROR should fire");
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Second W to verify dedup doesn't block different messages */
    ESP_LOGW(TAG, "second warning - different hash, should fire again");
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "TEST_ESPIDF_DONE");

    while (1) vTaskDelay(pdMS_TO_TICKS(5000));
}
