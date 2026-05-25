#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

static const char *TAG_RESET = "app_reset";

#ifndef CONFIG_RESET_BUTTON_GPIO
#define CONFIG_RESET_BUTTON_GPIO 0
#endif
#define RESET_BUTTON_GPIO CONFIG_RESET_BUTTON_GPIO
#define RESET_BUTTON_LONG_PRESS_MS 5000
#define RESET_BUTTON_DEBOUNCE_MS 50

static void IRAM_ATTR reset_button_isr(void *arg)
{
    static uint32_t last_interrupt = 0;
    uint32_t now = xTaskGetTickCountFromISR();
    if ((now - last_interrupt) * portTICK_PERIOD_MS < RESET_BUTTON_DEBOUNCE_MS) return;
    last_interrupt = now;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTimerStartFromISR((TimerHandle_t)arg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void reset_timer_cb(TimerHandle_t xTimer)
{
    uint32_t press_duration = 0;
    uint32_t start = xTimerGetExpiryTime(xTimer) - pdMS_TO_TICKS(RESET_BUTTON_LONG_PRESS_MS);

    while (gpio_get_level((gpio_num_t)RESET_BUTTON_GPIO) == 0) {
        press_duration = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (press_duration >= RESET_BUTTON_LONG_PRESS_MS) {
            ESP_LOGI(TAG_RESET, "Long press detected (%dms), factory reset...", press_duration);
            esp_matter::factory_reset();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (press_duration > 0) {
        ESP_LOGI(TAG_RESET, "Button released after %dms", press_duration);
    }
}

static esp_err_t app_reset_button_register(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RESET_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    TimerHandle_t timer = xTimerCreate("reset_timer", pdMS_TO_TICKS(RESET_BUTTON_LONG_PRESS_MS), pdFALSE, NULL, reset_timer_cb);
    if (!timer) {
        ESP_LOGE(TAG_RESET, "Failed to create reset timer");
        return ESP_FAIL;
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)RESET_BUTTON_GPIO, reset_button_isr, (void *)timer);

    ESP_LOGI(TAG_RESET, "Reset button registered on GPIO%d (long press %dms)", RESET_BUTTON_GPIO, RESET_BUTTON_LONG_PRESS_MS);
    return ESP_OK;
}
