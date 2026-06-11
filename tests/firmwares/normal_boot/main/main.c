#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "normal_boot";

void app_main(void)
{
    ESP_LOGI(TAG, "normal_boot: booting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    /* Signal detected by emon --wait-for BOOT_OK */
    ESP_LOGI(TAG, "BOOT_OK");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "heartbeat");
    }
}
