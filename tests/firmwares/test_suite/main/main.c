#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdlib.h>

static const char *TAG = "test_suite";

/* ── NVS helpers ──────────────────────────────────────────────────────── */

static uint8_t phase_get(void)
{
    nvs_handle_t h;
    uint8_t phase = 0;
    if (nvs_open("suite", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "phase", &phase);
        nvs_close(h);
    }
    return phase;
}

static void phase_set(uint8_t phase)
{
    nvs_handle_t h;
    if (nvs_open("suite", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "phase", phase);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ── Stack overflow hook ──────────────────────────────────────────────── */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    esp_rom_printf("***ERROR*** stack overflow in task %s\r\n", pcTaskName);
    esp_restart();   /* clean reboot - avoid triggering IWDT Guru Meditation */
}

/* ── Phase tasks ──────────────────────────────────────────────────────── */

static void overflow_task(void *arg)
{
    (void)arg;
    volatile char buf[1800];
    buf[0] = 1;
    (void)buf;
    vTaskDelay(1);   /* context switch → PTRVAL check → overflow hook */
    vTaskDelete(NULL);
}

static void hog_task(void *arg)
{
    (void)arg;
    while (1) { }   /* starves idle0 → watchdog fires in ~5s */
}

/* ── app_main ─────────────────────────────────────────────────────────── */

void app_main(void)
{
    /* Init NVS - erase if needed */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    uint8_t phase = phase_get();
    ESP_LOGI(TAG, "=== test_suite phase %d/4 ===", phase);
    vTaskDelay(pdMS_TO_TICKS(1000));

    switch (phase) {

    case 0:
        /* Signal boot OK, then trigger abort */
        ESP_LOGI(TAG, "TEST: BOOT_OK");
        vTaskDelay(pdMS_TO_TICKS(300));
        phase_set(1);
        ESP_LOGI(TAG, "phase 0: abort()");
        abort();
        break;

    case 1:
        /* Stack overflow via tiny-stack task + yield */
        phase_set(2);
        ESP_LOGI(TAG, "phase 1: stack overflow");
        xTaskCreate(overflow_task, "overflow", 2048, NULL, 5, NULL);
        vTaskDelay(pdMS_TO_TICKS(5000));
        break;

    case 2:
        /* CPU hog → watchdog fires on idle0 */
        phase_set(3);
        ESP_LOGI(TAG, "phase 2: task watchdog (~5s)");
        xTaskCreatePinnedToCore(hog_task, "hog", 2048, NULL,
                                configMAX_PRIORITIES - 1, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(30000));
        break;

    case 3:
        /* NULL deref → Guru Meditation Error */
        phase_set(4);
        ESP_LOGI(TAG, "phase 3: Guru Meditation");
        vTaskDelay(pdMS_TO_TICKS(300));
        {
            volatile int *p = NULL;
            *p = 0xDEAD;
        }
        break;

    case 4:
    default:
        /* All done - reset phase for next run */
        phase_set(0);
        ESP_LOGI(TAG, "TEST: ALL_DONE");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        break;
    }
}
