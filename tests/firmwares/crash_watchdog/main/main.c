#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "crash_wdt";

/* Spin at max priority on CPU0 - starves idle0 → TWDT fires after 5s */
static void hog_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "crash_watchdog: hogging CPU0...");
    while (1) { /* no yield */ }
}

void app_main(void)
{
    ESP_LOGI(TAG, "crash_watchdog: waiting 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "crash_watchdog: spawning CPU hog (watchdog fires in ~5s)...");

    xTaskCreatePinnedToCore(hog_task, "hog",
                            2048, NULL,
                            configMAX_PRIORITIES - 1,
                            NULL, 0 /* CPU0 */);

    /* app_main waits - watchdog fires on its own */
    vTaskDelay(pdMS_TO_TICKS(60000));
}
